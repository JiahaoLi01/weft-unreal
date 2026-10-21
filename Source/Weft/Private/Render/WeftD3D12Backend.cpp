#include "Render/WeftD3D12Backend.h"

#if WITH_WEFT && PLATFORM_WINDOWS

#include "ID3D12DynamicRHI.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"
#include "RHICommandList.h"
#include "Render/WeftFramePacket.h"
#include "Render/WeftOwnerThread.h"
#include "WeftModule.h"

namespace
{
	// The name weft's renderer gives the RHI's direct queue in its submissions.
	constexpr uint64 Queue = 1;
	// Draws an imported texture may go unnamed before it is released.
	constexpr uint64 UnusedDraws = 600;

	/** The weft format of an engine texture's pixels; false for one weft does not sample. */
	bool TextureFormat(const FRHITextureDesc& Desc, uint32& Out)
	{
		const bool bSrgb = EnumHasAnyFlags(Desc.Flags, ETextureCreateFlags::SRGB);
		switch (Desc.Format)
		{
		case PF_R8G8B8A8: Out = bSrgb ? WEFT_RENDER_TARGET_SRGB_RGBA8 : WEFT_RENDER_TARGET_LINEAR_RGBA8; return true;
		case PF_B8G8R8A8: Out = bSrgb ? WEFT_RENDER_TARGET_SRGB_BGRA8 : WEFT_RENDER_TARGET_LINEAR_BGRA8; return true;
		case PF_FloatRGBA: Out = WEFT_RENDER_TARGET_LINEAR_RGBA16F; return true;
		case PF_A32B32G32R32F: Out = WEFT_RENDER_TARGET_LINEAR_RGBA32F; return true;
		default: return false;
		}
	}
}

FWeftD3D12Backend::FWeftD3D12Backend()
	: Owner(MakeUnique<FWeftOwnerThread>())
{
}

FWeftD3D12Backend::~FWeftD3D12Backend()
{
	check(!Renderer);
}

bool FWeftD3D12Backend::Open(ID3D12Device* Device)
{
	const weft_render_d3d12_desc Desc{sizeof(Desc), WEFT_RENDER_ABI_VERSION, Device, Queue, 0};
	weft_render_backend Table{};
	if (const weft_render_result Result = weft_render_d3d12_create(&Desc, &Backend, &Table); Result != WEFT_RENDER_OK)
	{
		UE_LOG(LogWeft, Error, TEXT("Weft: no D3D12 backend (%d); views draw nothing."), int32(Result));
		Backend = nullptr;
		return false;
	}
	if (const weft_render_result Result = weft_renderer_create(&Table, nullptr, &Renderer); Result != WEFT_RENDER_OK)
	{
		UE_LOG(LogWeft, Error, TEXT("Weft: no D3D12 renderer (%d); views draw nothing."), int32(Result));
		weft_render_d3d12_destroy(Backend);
		Backend = nullptr;
		Renderer = nullptr;
		return false;
	}
	weft_render_info Info{sizeof(Info)};
	weft_renderer_get_info(Renderer, &Info);
	Identity = Info.identity;
	// The engine's textures, for -weft-texture(): weft samples them on the same device and queue.
	weft_render_external_backend External{};
	if (weft_render_d3d12_external(Backend, &External) != WEFT_RENDER_OK ||
		weft_renderer_install_external_backend(Renderer, &External) != WEFT_RENDER_OK)
	{
		UE_LOG(LogWeft, Warning, TEXT("Weft: no engine textures on D3D12; pages draw no -weft-texture()."));
	}
	if (FAILED(Device->CreateFence(0, D3D12_FENCE_FLAG_NONE, __uuidof(ID3D12Fence), reinterpret_cast<void**>(Fence.GetInitReference()))))
	{
		UE_LOG(LogWeft, Error, TEXT("Weft: no D3D12 fence; views draw nothing."));
		weft_renderer_destroy(Renderer);
		weft_render_d3d12_destroy(Backend);
		Backend = nullptr;
		Renderer = nullptr;
		return false;
	}
	UE_LOG(LogWeft, Log, TEXT("Weft: drawing with D3D12 on the RHI's device"));
	return true;
}

void FWeftD3D12Backend::Retire()
{
	const uint64 Done = Fence->GetCompletedValue();
	if (Done > Completed && Done <= Serial)
	{
		Completed = Done;
		weft_renderer_notify_completed(Renderer, Identity.device_epoch, Queue, Completed);
	}
}

const weft_render_handle* FWeftD3D12Backend::Import(const FNamedTexture& Named)
{
	if (FImported* Found = Imported.Find(Named.Texture))
	{
		Found->LastDraw = Draws;
		return &Found->Handle;
	}
	if (Refused.Contains(Named.Texture))
	{
		return nullptr;
	}
	if (!Named.Resource)
	{
		UE_LOG(LogWeft, Warning, TEXT("Weft: an engine texture of a format weft does not sample; pages draw nothing of it."));
		Refused.Add(Named.Texture);
		return nullptr;
	}
	// A render target's writes are queued on the RHI's direct queue between the frames that sample it, which is what
	// weft's live textures ask; the rest stay as they were imported.
	const weft_render_d3d12_texture Native{sizeof(Native), WEFT_RENDER_EXTERNAL_ABI_VERSION, Named.Resource, 0};
	const weft_render_external_texture Description{sizeof(Description), WEFT_RENDER_EXTERNAL_ABI_VERSION, Identity, NextContentId++, 1,
		uint32(Named.Size.X), uint32(Named.Size.Y), Named.Format, WEFT_RENDER_EXTERNAL_LINEAR_PREMULTIPLIED, WEFT_RENDER_EXTERNAL_TOP_LEFT,
		Named.bLive ? WEFT_RENDER_EXTERNAL_SAME_QUEUE_LIVE : WEFT_RENDER_EXTERNAL_SAME_QUEUE_ORDERED, Named.Mips, 0, &Native,
		sizeof(Native), nullptr, nullptr};
	weft_render_handle Handle{};
	if (const weft_render_result Result = weft_renderer_import_texture(Renderer, &Description, &Handle); Result != WEFT_RENDER_OK)
	{
		UE_LOG(LogWeft, Warning, TEXT("Weft: an engine texture (%dx%d) was not imported (%d); pages draw nothing of it."), Named.Size.X,
			Named.Size.Y, int32(Result));
		Refused.Add(Named.Texture);
		return nullptr;
	}
	FImported& Added = Imported.Add(Named.Texture);
	Added.Handle = Handle;
	Added.Texture = Named.Texture;
	Added.LastDraw = Draws;
	return &Added.Handle;
}

void FWeftD3D12Backend::ReleaseImports(bool bAll)
{
	for (auto It = Imported.CreateIterator(); It; ++It)
	{
		if (bAll || It->Value.LastDraw + UnusedDraws < Draws)
		{
			// Frames weft prepared, encoded or submitted with it keep using it until they complete.
			weft_renderer_release_texture(Renderer, It->Value.Handle);
			It.RemoveCurrent();
		}
	}
}

bool FWeftD3D12Backend::Record(ID3D12GraphicsCommandList* List, ID3D12Resource* Target, FIntPoint Size, uint32 Format,
	FWeftFramePacket& Packet, TConstArrayView<FNamedTexture> Named)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(Weft_Record);
	Retire();
	++Draws;
	// The engine's textures by the names the page draws them by.
	TArray<weft_render_scene_texture, TInlineAllocator<8>> Bound;
	for (int32 I = 0; I < Named.Num(); ++I)
	{
		if (const weft_render_handle* Handle = Import(Named[I]))
		{
			const TArray<ANSICHAR>& Name = Packet.Textures[I].Name;
			Bound.Add({Name.GetData(), uint32(Name.Num()), 0, *Handle});
		}
	}
	if (Draws % 64 == 0)
	{
		ReleaseImports(false);
	}
	// This frame's anchors, then its View frame.
	Packet.WriteAnchors();
	const weft_render_scene_frame Scene{sizeof(Scene), WEFT_RENDER_ABI_VERSION, Packet.Frame, 0, uint32(Bound.Num()), Bound.GetData()};
	weft_render_handle Prepared{};
	weft_render_result Result = weft_renderer_prepare_scene(Renderer, &Scene, &Prepared);
	Packet.ReleaseFrame();
	if (Result == WEFT_RENDER_OK)
	{
		weft_render_d3d12_pass Native{sizeof(Native), WEFT_RENDER_ABI_VERSION, List, Target, 0, 0, {}};
		const weft_render_pass Pass{sizeof(Pass), WEFT_RENDER_ABI_VERSION, Identity, uint32(Size.X), uint32(Size.Y), Format, 1,
			&Native, sizeof(Native)};
		weft_render_handle Use{};
		Result = weft_renderer_encode(Renderer, Prepared, &Pass, &Use);
		weft_renderer_release_prepared(Renderer, Prepared);
		if (Result == WEFT_RENDER_OK)
		{
			// The recording goes to the GPU with the RHI's command list.
			weft_renderer_notify_submitted(Renderer, Use, Queue, ++Serial);
			return true;
		}
	}
	// Why, as weft says (a frame its native pass cannot draw: what of the page it cannot), once each reason.
	FString Why;
	weft_renderer_last_failure(
		Renderer,
		[](void* User, const char* Text, size_t Size)
		{
			const FUTF8ToTCHAR Converted(Text, int32(Size));
			*static_cast<FString*>(User) = FString(Converted.Length(), Converted.Get());
		},
		&Why);
	const FString Reason = FString::Printf(TEXT("%d %s"), int32(Result), *Why);
	if (!WarnedDraws.Contains(Reason) && WarnedDraws.Num() < 64)
	{
		WarnedDraws.Add(Reason);
		UE_LOG(LogWeft, Warning, TEXT("Weft: a frame did not draw (%d)%s%s"), int32(Result), Why.IsEmpty() ? TEXT("") : TEXT(": "),
			*Why);
	}
	return false;
}

bool FWeftD3D12Backend::Draw(FRHICommandList& Executing, FRHITexture* Target, FIntPoint Size, uint32 Format,
	FWeftFramePacket& Packet)
{
	if (bFailed || !Packet.Frame)
	{
		return false;
	}
	ID3D12DynamicRHI* RHI = GetID3D12DynamicRHI();
	ID3D12Device* Device = RHI->RHIGetDevice(0);
	// The RHI's barriers (the target to a render target) before weft's commands.
	RHI->RHIFlushResourceBarriers(Executing, 0);
	ID3D12GraphicsCommandList* List = RHI->RHIGetGraphicsCommandList(Executing, 0);
	ID3D12Resource* Resource = RHI->RHIGetResource(Target);
	// The named textures' resources and pixels, from the RHI on this thread (in the shader-resource state the pass put
	// them in); a resource stays null for a texture weft cannot sample.
	TArray<FNamedTexture, TInlineAllocator<8>> Named;
	for (const FWeftPacketTexture& Texture : Packet.Textures)
	{
		FNamedTexture& Entry = Named.AddDefaulted_GetRef();
		const FRHITextureDesc& Desc = Texture.Texture->GetDesc();
		Entry.Texture = Texture.Texture;
		Entry.Size = FIntPoint(Desc.Extent.X, Desc.Extent.Y);
		Entry.Mips = Desc.NumMips;
		Entry.bLive = EnumHasAnyFlags(Desc.Flags, ETextureCreateFlags::RenderTargetable | ETextureCreateFlags::UAV);
		if (TextureFormat(Desc, Entry.Format) && Desc.Dimension == ETextureDimension::Texture2D)
		{
			Entry.Resource = RHI->RHIGetResource(Texture.Texture);
		}
	}
	bool bDrawn = false;
	Owner->Call([&]
		{
			if (!Renderer && !Open(Device))
			{
				bFailed = true;
				return;
			}
			bDrawn = Record(List, Resource, Size, Format, Packet, Named);
		});
	if (bDrawn)
	{
		// Closes the RHI's command list after weft's commands: the RHI sets its state again on the next one.
		RHI->RHISignalManualFence(Executing, Fence.GetReference(), Serial);
	}
	return bDrawn;
}

void FWeftD3D12Backend::Shutdown()
{
	Owner->Call([this]
		{
			if (Renderer)
			{
				ReleaseImports(true);
				// The GPU is idle: every recording completed.
				weft_renderer_notify_completed(Renderer, Identity.device_epoch, Queue, Serial);
				if (const weft_render_result Result = weft_renderer_destroy(Renderer); Result != WEFT_RENDER_OK)
				{
					UE_LOG(LogWeft, Warning, TEXT("Weft: the D3D12 renderer stays (%d)."), int32(Result));
				}
				Renderer = nullptr;
			}
			if (Backend)
			{
				weft_render_d3d12_destroy(Backend);
				Backend = nullptr;
			}
		});
	Fence.SafeRelease();
	Owner.Reset();
}

#endif
