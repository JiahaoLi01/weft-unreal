#pragma once

#include "CoreMinimal.h"

#include "WeftTypes.generated.h"

/**
 * Where a page's element with data-anchor="<Key>" goes this frame (docs/engine-communication.md of weft, section 5):
 * the engine projects, the renderer moves the element, with no script, style or layout work.
 */
USTRUCT(BlueprintType)
struct WEFT_API FWeftAnchor
{
	GENERATED_BODY()

	/** The key the element names in data-anchor (decimal). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weft")
	int64 Key = 0;

	/** Where the anchor is, in the view's logical px (CSS px from its top-left). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weft")
	FVector2D Position = FVector2D::ZeroVector;

	/** The element's scale about its pivot (data-anchor-pivot); 1 is its laid-out size. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weft")
	float Scale = 1.f;

	/** Larger is farther: anchored siblings draw far to near. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weft")
	float Depth = 0.f;

	/** Hidden this frame. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weft")
	bool bHidden = false;

	/** Behind the camera, or behind what the scene puts in front of it: hidden unless the page shows it anyway. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weft")
	bool bBehind = false;
};

/** A page for a weft View: its scripts in order, its stylesheet and keyframes, and the selector of its root. */
struct WEFT_API FWeftPage
{
	/** Script name (for stack traces) and source. */
	TArray<TPair<FString, FString>> Scripts;
	FString Stylesheet;
	/** The @keyframes rules: weft takes them apart from the stylesheet. */
	FString Keyframes;
	/** "#id" or a tag name; empty is "#root", the element the document's body starts with. */
	FString Root;
	/** Installs CRPG Framework's window.ue.sendEvent, __ue_dispatch and __vm_update over weft.engine. */
	bool bCrpgBridge = false;
	/** Takes the stylesheet as browsers take CSS (WEFT_PAGE_FORGIVING_CSS): what the scene cannot draw is dropped with a
	 *  diagnostic instead of refusing the page. Pages a build tool writes (Tailwind and the like) need it. */
	bool bForgivingCss = false;
	/** The web platform pages written for browsers expect (WEFT_PAGE_WEB_PLATFORM): the console as log lines,
	 *  localStorage (the View's, see UWeftView::SetStorageItem), dataset, matchMedia. The focus and the keyboard are
	 *  every page's. */
	bool bWebPlatform = false;
	/** Chrome's spatial navigation (WEFT_PAGE_SPATIAL_NAVIGATION): the arrow keys move the focus to the nearest element
	 *  their way (the pointer with it), Escape blurs it; and the gamepads' D-pad and left stick, A and B are the arrow
	 *  keys, Enter and Escape of the page with the keyboard focus. */
	bool bSpatialNavigation = false;
	/**
	 * Where the stylesheet's relative URLs (background-image: url()) start: its file's path, or a folder ending in '/'.
	 * The View reads the files they name from disk, or from a pak in a packaged game; empty, they resolve from the
	 * working directory.
	 */
	FString Base;

	/**
	 * The page in a folder: page.json ({"scripts": [...], "stylesheet": "...", "keyframes": "...", "root": "...",
	 * "crpgBridge": false, "forgivingCss": false, "webPlatform": false, "spatialNavigation": false}, paths relative to
	 * the folder), or without it
	 * page.js, with page.css and keyframes.css when they are there; its base the stylesheet's path. False, with why in
	 * OutError, when a file is missing.
	 */
	static bool FromDirectory(const FString& Directory, FWeftPage& Out, FString* OutError = nullptr);
};
