#pragma once

#include "CoreMinimal.h"
#include "UObject/WeakObjectPtrTemplates.h"

// The plugin's debug tools, in a build with weft's developer tools (WITH_WEFT_DEVTOOLS: the editor and every
// configuration but Shipping, and Shipping when the project asks for them; WeftLibrary.Build.cs). Without them none of
// this is in the binary.
#if WITH_WEFT_DEVTOOLS

class UWeftView;

/** A View as the debug tools show it: weft.Views, weft.Debug and the editor's Weft panel. */
struct FWeftViewDebugInfo
{
	TWeakObjectPtr<UWeftView> View;
	/** The page's name (UWeftView::GetPageName); empty before a page loads. */
	FString Page;
	/** The View's page in weft's developer tools: /devtools/page/<Target> (its slot and generation). */
	FString Target;
	/** Logical px, the device px a logical px spans, and the CSS px per unit of its widget (UWeftView::SetContentScale). */
	FVector2D Size = FVector2D::ZeroVector;
	float DeviceScale = 1.f;
	float ContentScale = 1.f;
	/** Elements in the page's document, counted about once a second; -1 before a page loads. */
	int32 Elements = -1;
	/** Whether the page's frames change by themselves (CSS animations and transitions, requestAnimationFrame). */
	bool bAnimated = false;
	/** ms per engine frame over the last whole second: weft's work for the View on the game thread (its frame, input,
	 *  the frame handed to the renderer) and the recording of its draw on the RHI thread; average and slowest frame. */
	float GameMs = 0.f;
	float GameMaxMs = 0.f;
	float DrawMs = 0.f;
	float DrawMaxMs = 0.f;
	/** The page's exceptions and console errors, and its warnings (styles its scene cannot take, console warnings),
	 *  since it loaded. */
	int32 Errors = 0;
	int32 Warnings = 0;
	/** Where Chrome opens the page in weft's developer tools; empty while they do not listen. */
	FString DevToolsUrl;
};

/**
 * The plugin's debug tools: weft's developer tools (Chrome DevTools on a View's page, weft's docs/devtools.md), and the
 * table of Views (what each is and costs). Game thread.
 *
 * Console: weft.DevTools [stop | [address:]port], weft.Views, weft.Debug 1. Command line: -WeftDevTools[=[address:]port]
 * and -WeftDebug (a Shipping build with the tools has no console). weft's own WEFT_DEVTOOLS_LISTEN works too.
 *
 * The costs are measured only while something shows them, and for two seconds after.
 */
class WEFT_API FWeftDebugTools
{
public:
	/**
	 * Starts weft's developer tools. Listen is "[address:]port": address 127.0.0.1 (this machine, the default) or 0.0.0.0
	 * (every interface, for a page on another device), port 0 or none for the first free one from 9230. Logs where Chrome
	 * connects. False, with why in OutError, when they do not start (no runtime, the port is taken, they listen already).
	 */
	static bool StartDevTools(const FString& Listen = FString(), FString* OutError = nullptr);
	static void StopDevTools();
	/** The port weft's developer tools listen on; 0 when they do not. */
	static int32 GetDevToolsPort();
	/** "127.0.0.1:9230", or "0.0.0.0:9230" when they listen on every interface; empty when they do not listen. */
	static FString GetDevToolsAddress();

	/** The live Views, and what they cost. Asking keeps the costs measured for the next two seconds. */
	static void GetViews(TArray<FWeftViewDebugInfo>& Out);
	/** The Views as a table of lines, the header first: what weft.Views logs and weft.Debug draws. */
	static TArray<FString> FormatViews(const TArray<FWeftViewDebugInfo>& Views);

	/** The table on the game's screen (weft.Debug): its own layer, drawn only while shown. */
	static void ShowOverlay(bool bShow);
	static bool IsOverlayShown();
};

#endif
