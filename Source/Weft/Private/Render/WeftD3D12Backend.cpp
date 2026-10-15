#include "Render/WeftD3D12Backend.h"

#if WITH_WEFT && PLATFORM_WINDOWS

#include "ID3D12DynamicRHI.h"
#include "RHICommandList.h"
#include "Render/WeftFramePacket.h"
#include "Render/WeftOwnerThread.h"
#include "WeftModule.h"

namespace
{
	// The name weft's renderer gives the RHI's direct queue in its submissions.
	constexpr uint64 Queue = 1;
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

bool FWeftD3D12Backend::Record(ID3D12GraphicsCommandList* List, ID3D12Resource* Target, FIntPoint Size, uint32 Format,
	FWeftFramePacket& Packet)
{
	Retire();
	// This frame's anchors, then its View frame.
	Packet.WriteAnchors();
	const weft_render_scene_frame Scene{sizeof(Scene), WEFT_RENDER_ABI_VERSION, Packet.Frame, 0, 0, nullptr};
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
	if (!bWarnedDraw)
	{
		UE_LOG(LogWeft, Warning, TEXT("Weft: a frame did not draw (%d)."), int32(Result));
		bWarnedDraw = true;
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
	bool bDrawn = false;
	Owner->Call([&]
		{
			if (!Renderer && !Open(Device))
			{
				bFailed = true;
				return;
			}
			bDrawn = Record(List, Resource, Size, Format, Packet);
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
