#include "Render/WeftRenderer.h"

#include "RHICommandList.h"
#include "RenderingThread.h"
#include "Render/WeftFramePacket.h"
#include "WeftModule.h"

#if WITH_WEFT
#include "weft/render_backend.h"
#endif

#if WITH_WEFT && PLATFORM_WINDOWS
#include "Render/WeftD3D12Backend.h"
#endif

namespace
{
	TUniquePtr<IWeftRhiBackend> CreateBackend()
	{
		switch (RHIGetInterfaceType())
		{
#if WITH_WEFT && PLATFORM_WINDOWS
		case ERHIInterfaceType::D3D12:
			return MakeUnique<FWeftD3D12Backend>();
#endif
		default:
			// Vulkan: weft's Vulkan backend on the RHI's device and command buffers; Metal once weft has a backend.
			UE_LOG(LogWeft, Warning, TEXT("Weft: no backend for the %s RHI yet (D3D12 now); views draw nothing."),
				GDynamicRHI ? GDynamicRHI->GetName() : TEXT("current"));
			return nullptr;
		}
	}
} // namespace

FWeftRenderer::~FWeftRenderer()
{
	check(!Backend);
}

bool FWeftRenderer::TargetFormat(EPixelFormat Format, uint32& Out)
{
#if WITH_WEFT
	switch (Format)
	{
	case PF_B8G8R8A8:
		Out = WEFT_RENDER_TARGET_LINEAR_BGRA8;
		return true;
	case PF_R8G8B8A8:
		Out = WEFT_RENDER_TARGET_LINEAR_RGBA8;
		return true;
	case PF_A2B10G10R10:
		// The default back buffer of Unreal 5 (R10G10B10A2_UNORM).
		Out = WEFT_RENDER_TARGET_LINEAR_RGB10A2;
		return true;
	default:
		return false;
	}
#else
	return false;
#endif
}

void FWeftRenderer::Draw(FRHICommandList& RHICmdList, FRHITexture* Target, FIntPoint Size, uint32 Format,
	const TSharedPtr<FWeftFramePacket, ESPMode::ThreadSafe>& Packet)
{
	RHICmdList.EnqueueLambda(TEXT("Weft"),
		[Self = AsShared(), TargetRef = FTextureRHIRef(Target), Size, Format, Packet](FRHICommandList& Executing)
		{
			if (!Self->bChosen)
			{
				Self->Backend = CreateBackend();
				Self->bChosen = true;
			}
			if (Self->Backend)
			{
				Self->Backend->Draw(Executing, TargetRef.GetReference(), Size, Format, *Packet);
			}
		});
}

void FWeftRenderer::Shutdown()
{
	check(IsInGameThread());
	ENQUEUE_RENDER_COMMAND(WeftShutdown)(
		[Self = AsShared()](FRHICommandListImmediate& RHICmdList)
		{
			// Every recording reaches the GPU and completes; then the backend goes on the thread that drew with it.
			RHICmdList.SubmitAndBlockUntilGPUIdle();
			RHICmdList.EnqueueLambda(TEXT("WeftShutdown"),
				[Self](FRHICommandList&)
				{
					if (Self->Backend)
					{
						Self->Backend->Shutdown();
						Self->Backend.Reset();
					}
				});
			RHICmdList.ImmediateFlush(EImmediateFlushType::FlushRHIThread);
		});
	FlushRenderingCommands();
}
