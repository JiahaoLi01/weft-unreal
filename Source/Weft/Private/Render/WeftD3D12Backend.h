#pragma once

#include "Render/WeftRenderer.h"

#if WITH_WEFT && PLATFORM_WINDOWS

#include "Windows/AllowWindowsPlatformTypes.h"
#include <d3d12.h>
#include "Windows/HideWindowsPlatformTypes.h"
#include "Templates/RefCounting.h"
#include "weft/render_backend.h"
#include "weft/render_backend_d3d12.h"

class FWeftOwnerThread;

/**
 * weft's D3D12 backend (render_backend_d3d12.h) on the D3D12 RHI's device: a frame is recorded into the RHI's own
 * command list, after its barriers, where the RHI stands. A manual fence signalled after each recording closes the
 * RHI's command list (so the RHI sets all its state again) and tells weft when the GPU completed the recording. The
 * RHI's calls run on the thread that executes the command list; weft's on the renderer's owner thread, which the
 * executing thread waits for.
 */
class FWeftD3D12Backend final : public IWeftRhiBackend
{
public:
	FWeftD3D12Backend();
	virtual ~FWeftD3D12Backend() override;
	virtual bool Draw(FRHICommandList& Executing, FRHITexture* Target, FIntPoint Size, uint32 Format, FWeftFramePacket& Packet) override;
	virtual void Shutdown() override;

private:
	// On the owner thread.
	/** The renderer on Device; false (logged) when weft refuses it. */
	bool Open(ID3D12Device* Device);
	/** Hands weft the recordings the GPU completed. */
	void Retire();
	/** Records the frame; true when it drew, the recording's serial then Serial. */
	bool Record(ID3D12GraphicsCommandList* List, ID3D12Resource* Target, FIntPoint Size, uint32 Format, FWeftFramePacket& Packet);

	TUniquePtr<FWeftOwnerThread> Owner;
	weft_render_d3d12* Backend = nullptr;
	weft_renderer* Renderer = nullptr;
	weft_render_identity Identity{};
	TRefCountPtr<ID3D12Fence> Fence;
	// Recordings signalled, and the last one the GPU completed.
	uint64 Serial = 0;
	uint64 Completed = 0;
	bool bFailed = false;
	bool bWarnedDraw = false;
};

#endif
