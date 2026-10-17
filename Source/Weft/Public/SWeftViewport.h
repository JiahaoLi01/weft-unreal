#pragma once

#include "CoreMinimal.h"
#include "UObject/WeakObjectPtrTemplates.h"
#include "Widgets/SLeafWidget.h"

class FWeftSlateDrawer;
class UWeftView;

/**
 * Where an interactive layer takes the pointer's buttons: wherever its page has an element that takes pointer events
 * (Element), or only where that element shows a cursor other than the default arrow (Cursor) — for a page whose panels
 * cover the screen with the game showing through, whose clickable parts say so with cursor: pointer and the like. Its
 * wheel reaches the page wherever it has an element (a panel scrolls under the arrow); with Cursor the game gets it too
 * unless the cursor there is not the arrow.
 */
enum class EWeftPointerRouting : uint8
{
	Element,
	Cursor
};

/**
 * Views on screen, as layers: each sized to the widget (logical px are Slate units, device px Slate's pixels), advanced
 * every frame and drawn where the widget is in Slate's order, a layer of a higher order over a lower one.
 *
 * Slate gives a point's buttons and wheel to the topmost widget there only, so pages that should each take the pointer
 * over their own panels (a tile panel on the left, a city panel on the right) belong on one widget as layers, not on
 * widgets of their own: the widget gives the buttons and the wheel to the topmost interactive layer whose page has an
 * element there that takes pointer events (CSS pointer-events; the root is the page, not something on it), and lets them
 * pass where none has (to the game, under a game viewport). The pointer's moves reach every layer, and the game. A
 * layer that is not interactive (labels, say) never takes the pointer; a widget with no interactive layer is invisible to
 * it.
 */
class WEFT_API SWeftViewport : public SLeafWidget
{
public:
	SLATE_BEGIN_ARGS(SWeftViewport) {}
		/** A first layer (more can be added); none by default. */
		SLATE_ARGUMENT_DEFAULT(UWeftView*, View) { nullptr };
		/** Whether that first layer takes pointer input; false for a page that only shows things. */
		SLATE_ARGUMENT_DEFAULT(bool, Interactive) { true };
		/** How that first layer takes the pointer. */
		SLATE_ARGUMENT_DEFAULT(EWeftPointerRouting, Routing) { EWeftPointerRouting::Element };
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	/** A view as a layer of the given order (a view added again changes its order, interactivity and routing). The view
	 *  lays out in the widget's units divided by its content scale (UWeftView::SetContentScale). */
	void AddView(UWeftView* InView, int32 Order = 0, bool bInteractive = true,
		EWeftPointerRouting Routing = EWeftPointerRouting::Element);
	void RemoveView(const UWeftView* InView);
	int32 NumViews() const { return Layers.Num(); }

	/** The View's logical px of a point in the widget's window, in Slate's pixels (a viewport's own pixels for a widget
	 *  that covers a game viewport); the widget's last geometry. */
	FVector2D PixelToLogical(FVector2D WindowPixel) const;

	/** Whether the pointer was over an element of an interactive layer's page that takes it, at its last move over the
	 *  widget (any layer's, or one view's): the game may ignore the pointer then, as it gets no clicks there. */
	bool IsPointerOverPage() const;
	bool IsPointerOverView(const UWeftView* InView) const;

	virtual void Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime) override;
	virtual int32 OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect,
		FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle,
		bool bParentEnabled) const override;
	virtual FVector2D ComputeDesiredSize(float) const override { return FVector2D::ZeroVector; }

	virtual FReply OnMouseMove(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	virtual FReply OnMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	virtual FReply OnMouseButtonUp(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	virtual FReply OnMouseWheel(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	virtual void OnMouseLeave(const FPointerEvent& MouseEvent) override;
	virtual FCursorReply OnCursorQuery(const FGeometry& MyGeometry, const FPointerEvent& CursorEvent) const override;

private:
	struct FLayer
	{
		TWeakObjectPtr<UWeftView> View;
		TSharedPtr<FWeftSlateDrawer, ESPMode::ThreadSafe> Drawer;
		int32 Order = 0;
		bool bInteractive = true;
		EWeftPointerRouting Routing = EWeftPointerRouting::Element;
		bool bPointerOver = false;
	};

	/** The topmost interactive layer whose page takes the pointer at a point of the widget (its units), and its cursor;
	 *  INDEX_NONE for none. bWheel: where the page has an element at all (the wheel reaches it whatever its routing). */
	int32 LayerTakingPointer(FVector2D Local, FString* OutCursor = nullptr, bool bWheel = false) const;
	static bool TakesPointer(const UWeftView* Page, FVector2D Local, FString* OutCursor = nullptr);
	/** Whether a cursor of the page says something there takes clicks: not the default arrow. */
	static bool InteractiveCursor(const FString& Cursor);
	/** A point of the widget (its units) in a page's logical px. */
	static FVector2D ToPage(const UWeftView* Page, FVector2D Local);
	/** Visible while a layer is interactive, else invisible to the pointer. */
	void UpdateVisibility();
	static uint32 Buttons(const FPointerEvent& MouseEvent);

	/** By order, lowest first. */
	TArray<FLayer> Layers;
	/** The view that took a button down, which gets the pointer until every button is up. */
	TWeakObjectPtr<UWeftView> Captor;
	FVector2D LastPosition = FVector2D::ZeroVector;
	float LastScale = 1.f;
};
