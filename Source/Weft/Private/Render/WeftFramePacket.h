#pragma once

#include "CoreMinimal.h"
#include "RHIResources.h"

#if WITH_WEFT
#include "weft/weft.h"
#endif

/** A texture of the engine a page may draw by name (-weft-texture(Name)), as the render thread has it this frame. */
struct FWeftPacketTexture
{
	/** UTF-8, as weft takes names. */
	TArray<ANSICHAR> Name;
	FTextureRHIRef Texture;
};

/**
 * What the render thread draws of a View in one frame: the frame the View published last (a reference, released with
 * the packet), the anchors the game set for this frame, written into the View just before the frame is encoded, so
 * that the anchored elements stand where this frame's camera put them, and the engine's named textures.
 */
struct FWeftFramePacket
{
	TArray<FWeftPacketTexture> Textures;

#if WITH_WEFT
	weft_runtime* Runtime = nullptr;
	weft_view_handle View{};
	weft_view_frame* Frame = nullptr;
	TArray<weft_anchor> Anchors;

	~FWeftFramePacket()
	{
		ReleaseFrame();
	}

	void ReleaseFrame()
	{
		if (Frame)
		{
			weft_view_frame_release(Frame);
			Frame = nullptr;
		}
	}

	/** Writes the anchors into the View (any thread; the render side is the View's one writer). */
	void WriteAnchors() const
	{
		weft_view_write_anchors(Runtime, View, Anchors.GetData(), Anchors.Num());
	}
#endif
};
