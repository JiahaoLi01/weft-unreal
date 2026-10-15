#pragma once

#include "CoreMinimal.h"
#include "UObject/WeakObjectPtrTemplates.h"
#include "Widgets/SLeafWidget.h"

class FWeftSlateDrawer;
class UWeftView;

/**
 * A View on screen: sized to the widget (logical px are Slate units, device px Slate's pixels), advanced every frame,
 * drawn where the widget is in Slate's order. The pointer goes to the page over elements that show an interactive
 * cursor (anything but auto, default and none); elsewhere it passes on (to the game, under a game viewport).
 */
class WEFT_API SWeftViewport : public SLeafWidget
{
public:
	SLATE_BEGIN_ARGS(SWeftViewport) {}
		SLATE_ARGUMENT(UWeftView*, View)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	/** The View's logical px of a point in the widget's window, in Slate's pixels (a viewport's own pixels for a widget
	 *  that covers a game viewport); the widget's last geometry. */
	FVector2D PixelToLogical(FVector2D WindowPixel) const;

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
	/** Whether the page takes the pointer at a point of the widget (logical px). */
	bool Interactive(FVector2D Local, FString* OutCursor = nullptr) const;
	static uint32 Buttons(const FPointerEvent& MouseEvent);

	TWeakObjectPtr<UWeftView> View;
	TSharedPtr<FWeftSlateDrawer, ESPMode::ThreadSafe> Drawer;
	FVector2D LastPosition = FVector2D::ZeroVector;
	float LastScale = 1.f;
};
