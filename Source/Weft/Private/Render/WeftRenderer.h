#pragma once

#include "CoreMinimal.h"
#include "PixelFormat.h"
#include "RHIFwd.h"

struct FWeftFramePacket;
class FRHICommandList;
class FRHICommandListBase;

/**
 * weft's backend of one RHI (weft's render_backend_<api>.h): it draws Views' frames into RHI textures, recording into
 * the RHI's own command lists. One per RHI: D3D12 now; Vulkan (weft's Vulkan backend on the RHI's device) and Metal
 * (once weft has one) go here too. All on the thread that executes the RHI's command lists.
 */
class IWeftRhiBackend
{
public:
	virtual ~IWeftRhiBackend() = default;

	/** Records Packet's frame into Target (Size device px at its top-left; Format a weft_render_target_format) where the
	 *  executing command list stands; false when nothing was drawn. */
	virtual bool Draw(FRHICommandList& Executing, FRHITexture* Target, FIntPoint Size, uint32 Format, FWeftFramePacket& Packet) = 0;

	/** The GPU finished everything recorded: releases the backend's renderer. */
	virtual void Shutdown() = 0;
};

/**
 * The render side of a weft runtime: draws its Views' frames with the backend of the running RHI, chosen on the thread
 * that executes the RHI's command lists the first time a frame is drawn.
 */
class FWeftRenderer : public TSharedFromThis<FWeftRenderer, ESPMode::ThreadSafe>
{
public:
	~FWeftRenderer();

	/** Render thread, from an RDG pass that writes Target as a render target: draws Packet's frame where the command
	 *  list stands. */
	void Draw(FRHICommandList& RHICmdList, FRHITexture* Target, FIntPoint Size, uint32 Format,
		const TSharedPtr<FWeftFramePacket, ESPMode::ThreadSafe>& Packet);

	/** Game thread: waits for the GPU, then releases the backend. */
	void Shutdown();

	/** The weft target format (weft_render_target_format) a pixel format is drawn as; false for one weft does not
	 *  draw into. */
	static bool TargetFormat(EPixelFormat Format, uint32& Out);

private:
	// The thread that executes the RHI's command lists alone.
	TUniquePtr<IWeftRhiBackend> Backend;
	bool bChosen = false;
};
