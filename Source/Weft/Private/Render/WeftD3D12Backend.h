#pragma once

#include "Render/WeftRenderer.h"

#if WITH_WEFT && PLATFORM_WINDOWS

#include "Windows/AllowWindowsPlatformTypes.h"
#include <d3d12.h>
#include "Windows/HideWindowsPlatformTypes.h"
#include "Templates/RefCounting.h"
#include "weft/render_backend.h"
#include "weft/render_backend_d3d12.h"
#include "weft/render_external_texture.h"

class FWeftOwnerThread;

/**
 * weft's D3D12 backend (render_backend_d3d12.h) on the D3D12 RHI's device: a frame is recorded into the RHI's own
 * command list, after its barriers, where the RHI stands. A manual fence signalled after each recording closes the
 * RHI's command list (so the RHI sets all its state again) and tells weft when the GPU completed the recording. The
 * RHI's calls run on the thread that executes the command list; weft's on the renderer's owner thread, which the
 * executing thread waits for. The engine's named textures (-weft-texture) are imported into weft the first time a frame
 * names them, and released when no frame has named them for a while: one the engine can draw into (a render target a
 * scene capture draws every frame) as live, the others as frozen.
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
	/** An engine texture a frame names, as the executing thread found it. */
	struct FNamedTexture
	{
		FRHITexture* Texture = nullptr;
		ID3D12Resource* Resource = nullptr;
		uint32 Format = 0;
		FIntPoint Size = FIntPoint::ZeroValue;
		uint32 Mips = 1;
		/** The engine may draw into it again between the frames that sample it. */
		bool bLive = false;
	};
	/** Records the frame; true when it drew, the recording's serial then Serial. */
	bool Record(ID3D12GraphicsCommandList* List, ID3D12Resource* Target, FIntPoint Size, uint32 Format, FWeftFramePacket& Packet,
		TConstArrayView<FNamedTexture> Named);
	/** A texture's import into weft, made the first time; null when weft refuses it (logged once). */
	const weft_render_handle* Import(const FNamedTexture& Named);
	/** Releases the imports no frame named for a while, or all of them. */
	void ReleaseImports(bool bAll);

	/** The engine textures weft holds, by RHI texture (holding it), and when a frame last named each. */
	struct FImported
	{
		weft_render_handle Handle{};
		FTextureRHIRef Texture;
		uint64 LastDraw = 0;
	};
	TMap<FRHITexture*, FImported> Imported;
	TSet<FRHITexture*> Refused;
	uint64 Draws = 0;
	uint64 NextContentId = 1;

	TUniquePtr<FWeftOwnerThread> Owner;
	weft_render_d3d12* Backend = nullptr;
	weft_renderer* Renderer = nullptr;
	weft_render_identity Identity{};
	TRefCountPtr<ID3D12Fence> Fence;
	// Recordings signalled, and the last one the GPU completed.
	uint64 Serial = 0;
	uint64 Completed = 0;
	bool bFailed = false;
	// The reasons of frames that did not draw, logged once each.
	TSet<FString> WarnedDraws;
};

#endif
