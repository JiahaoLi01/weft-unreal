#include "SWeftViewport.h"

#include "Framework/Application/SlateApplication.h"
#include "HAL/IConsoleManager.h"
#include "Render/WeftFramePacket.h"
#include "Render/WeftSlateDrawer.h"
#include "RenderingThread.h"
#include "Rendering/DrawElements.h"
#include "WeftSubsystem.h"
#include "WeftView.h"

namespace
{
	TAutoConsoleVariable<int32> CVarWeftDraw(TEXT("weft.Draw"), 1,
		TEXT("Draw weft views (0: their pages still run, nothing is drawn)."), ECVF_Default);
}

void SWeftViewport::Construct(const FArguments& InArgs)
{
	View = InArgs._View;
	if (const UWeftSubsystem* Weft = UWeftSubsystem::Get())
	{
		if (Weft->GetRenderer())
		{
			Drawer = MakeShared<FWeftSlateDrawer, ESPMode::ThreadSafe>(Weft->GetRenderer());
		}
	}
	// The page moves every frame (its anchors at least): paint every frame.
	SetCanTick(true);
	ForceVolatile(true);
}

FVector2D SWeftViewport::PixelToLogical(FVector2D WindowPixel) const
{
	return (WindowPixel - LastPosition) / LastScale;
}

void SWeftViewport::Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime)
{
	LastPosition = AllottedGeometry.GetAbsolutePosition();
	LastScale = AllottedGeometry.Scale;
	if (UWeftView* Page = View.Get())
	{
		Page->Resize(AllottedGeometry.GetLocalSize(), AllottedGeometry.Scale);
		Page->Advance();
	}
}

int32 SWeftViewport::OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect,
	FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const
{
	const UWeftView* Page = View.Get();
	if (!Page || !Drawer || !CVarWeftDraw.GetValueOnGameThread())
	{
		return LayerId;
	}
	TSharedPtr<FWeftFramePacket, ESPMode::ThreadSafe> Packet = Page->MakePacket();
	if (!Packet)
	{
		return LayerId;
	}
	// The widget in Slate's pixels: where the View's frame goes, at the View's device scale.
	const FVector2D Position = AllottedGeometry.GetAbsolutePosition();
	const FIntPoint Min(FMath::RoundToInt(Position.X), FMath::RoundToInt(Position.Y));
	const FVector2D Size = AllottedGeometry.GetLocalSize() * Page->GetDeviceScale();
	const FIntRect Rect(Min, Min + FIntPoint(FMath::CeilToInt(Size.X), FMath::CeilToInt(Size.Y)));
	ENQUEUE_RENDER_COMMAND(WeftPending)(
		[Drawer = Drawer, Packet = MoveTemp(Packet), Rect](FRHICommandListImmediate&) mutable
		{
			Drawer->SetPending(MoveTemp(Packet), Rect);
		});
	FSlateDrawElement::MakeCustom(OutDrawElements, LayerId, Drawer);
	return LayerId + 1;
}

uint32 SWeftViewport::Buttons(const FPointerEvent& MouseEvent)
{
	// MouseEvent.buttons: primary 1, secondary 2, middle 4.
	return (MouseEvent.IsMouseButtonDown(EKeys::LeftMouseButton) ? 1u : 0u) |
		(MouseEvent.IsMouseButtonDown(EKeys::RightMouseButton) ? 2u : 0u) |
		(MouseEvent.IsMouseButtonDown(EKeys::MiddleMouseButton) ? 4u : 0u);
}

bool SWeftViewport::Interactive(FVector2D Local, FString* OutCursor) const
{
	const UWeftView* Page = View.Get();
	FString Cursor;
	const bool bElement = Page && Page->HitTest(Local, Cursor);
	if (OutCursor)
	{
		*OutCursor = Cursor;
	}
	return bElement && Cursor != TEXT("auto") && Cursor != TEXT("default") && Cursor != TEXT("none");
}

FReply SWeftViewport::OnMouseMove(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	const FVector2D Local = MyGeometry.AbsoluteToLocal(MouseEvent.GetScreenSpacePosition());
	if (UWeftView* Page = View.Get())
	{
		Page->Pointer(EWeftPointer::Move, Local, Buttons(MouseEvent));
	}
	// The game sees the pointer move too, unless the page holds it.
	return HasMouseCapture() ? FReply::Handled() : FReply::Unhandled();
}

FReply SWeftViewport::OnMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	const FVector2D Local = MyGeometry.AbsoluteToLocal(MouseEvent.GetScreenSpacePosition());
	UWeftView* Page = View.Get();
	if (!Page || !Interactive(Local))
	{
		return FReply::Unhandled();
	}
	Page->Pointer(EWeftPointer::Down, Local, Buttons(MouseEvent));
	return FReply::Handled().CaptureMouse(AsShared());
}

FReply SWeftViewport::OnMouseButtonUp(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	if (!HasMouseCapture())
	{
		return FReply::Unhandled();
	}
	if (UWeftView* Page = View.Get())
	{
		Page->Pointer(EWeftPointer::Up, MyGeometry.AbsoluteToLocal(MouseEvent.GetScreenSpacePosition()), Buttons(MouseEvent));
	}
	return Buttons(MouseEvent) ? FReply::Handled() : FReply::Handled().ReleaseMouseCapture();
}

FReply SWeftViewport::OnMouseWheel(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	const FVector2D Local = MyGeometry.AbsoluteToLocal(MouseEvent.GetScreenSpacePosition());
	UWeftView* Page = View.Get();
	if (!Page || !Interactive(Local))
	{
		return FReply::Unhandled();
	}
	// Slate's wheel is in notches, upward positive; the page's in px, downward positive (100 px a notch, as browsers).
	Page->Pointer(EWeftPointer::Wheel, Local, Buttons(MouseEvent), FVector2D(0, -MouseEvent.GetWheelDelta() * 100.0));
	return FReply::Handled();
}

void SWeftViewport::OnMouseLeave(const FPointerEvent& MouseEvent)
{
	if (UWeftView* Page = View.Get())
	{
		Page->Pointer(EWeftPointer::Leave, FVector2D::ZeroVector, 0);
	}
}

FCursorReply SWeftViewport::OnCursorQuery(const FGeometry& MyGeometry, const FPointerEvent& CursorEvent) const
{
	FString Cursor;
	if (!Interactive(MyGeometry.AbsoluteToLocal(CursorEvent.GetScreenSpacePosition()), &Cursor))
	{
		return FCursorReply::Unhandled();
	}
	if (Cursor == TEXT("pointer"))
	{
		return FCursorReply::Cursor(EMouseCursor::Hand);
	}
	if (Cursor == TEXT("text"))
	{
		return FCursorReply::Cursor(EMouseCursor::TextEditBeam);
	}
	if (Cursor == TEXT("grab") || Cursor == TEXT("grabbing") || Cursor == TEXT("move"))
	{
		return FCursorReply::Cursor(Cursor == TEXT("grabbing") ? EMouseCursor::GrabHandClosed : EMouseCursor::GrabHand);
	}
	return FCursorReply::Cursor(EMouseCursor::Default);
}
