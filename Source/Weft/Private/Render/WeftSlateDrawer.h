#pragma once

#include "CoreMinimal.h"
#include "Rendering/RenderingCommon.h"

class FWeftRenderer;
struct FWeftFramePacket;

/**
 * Draws a View's frame where its widget is in Slate's output, in Slate's order (over what Slate drew before the
 * widget). A widget not at the output's top-left draws in a texture of its own size: the output's pixels there are
 * copied in (weft draws over them, and its backdrop filters read them), drawn over, and copied back.
 */
class FWeftSlateDrawer : public ICustomSlateElement
{
public:
	explicit FWeftSlateDrawer(TSharedPtr<FWeftRenderer, ESPMode::ThreadSafe> InRenderer);

	/** Render thread: what the next draw draws, and where (the widget's rect in Slate's pixels). */
	void SetPending(TSharedPtr<FWeftFramePacket, ESPMode::ThreadSafe> Packet, FIntRect Rect);

	virtual void Draw_RenderThread(FRDGBuilder& GraphBuilder, const FDrawPassInputs& Inputs) override;

private:
	TSharedPtr<FWeftRenderer, ESPMode::ThreadSafe> Renderer;
	// Render thread.
	TSharedPtr<FWeftFramePacket, ESPMode::ThreadSafe> Pending;
	FIntRect PendingRect;
	bool bWarnedFormat = false;
};
