#include "SWeftViewport.h"

#include "Framework/Application/SlateApplication.h"
#include "HAL/IConsoleManager.h"
#include "Render/WeftFramePacket.h"
#include "Render/WeftSlateDrawer.h"
#include "RenderingThread.h"
#include "Rendering/DrawElements.h"
#include "TextureResource.h"
#include "WeftSubsystem.h"
#include "WeftView.h"

namespace
{
	TAutoConsoleVariable<int32> CVarWeftDraw(TEXT("weft.Draw"), 1,
		TEXT("Draw weft views (0: their pages still run, nothing is drawn)."), ECVF_Default);

	// How often a page takes a move of a pointer that stands still (Slate sends one as widgets move under it): each
	// is a hit test of the page's latest frame.
	constexpr double StillMoveSeconds = 0.1;
}

void SWeftViewport::Construct(const FArguments& InArgs)
{
	// The pages move every frame (their anchors at least): paint every frame.
	SetCanTick(true);
	ForceVolatile(true);
	if (InArgs._View)
	{
		AddView(InArgs._View, 0, InArgs._Interactive, InArgs._Routing);
	}
	UpdateVisibility();
}

void SWeftViewport::AddView(UWeftView* InView, int32 Order, bool bInteractive, EWeftPointerRouting Routing)
{
	if (!InView)
	{
		return;
	}
	RemoveView(InView);
	FLayer Layer;
	Layer.View = InView;
	Layer.Order = Order;
	Layer.bInteractive = bInteractive;
	Layer.Routing = Routing;
	if (const UWeftSubsystem* Weft = UWeftSubsystem::Get())
	{
		if (Weft->GetRenderer())
		{
			Layer.Drawer = MakeShared<FWeftSlateDrawer, ESPMode::ThreadSafe>(Weft->GetRenderer());
		}
	}
	// After the layers of its order or lower: the later of one order draws over the earlier.
	const int32 At = Layers.IndexOfByPredicate([Order](const FLayer& Other) { return Other.Order > Order; });
	Layers.Insert(MoveTemp(Layer), At == INDEX_NONE ? Layers.Num() : At);
	UpdateVisibility();
}

void SWeftViewport::RemoveView(const UWeftView* InView)
{
	Layers.RemoveAll([InView](const FLayer& Layer) { return Layer.View.Get() == InView || !Layer.View.IsValid(); });
	if (Captor.Get() == InView)
	{
		Captor.Reset();
	}
	UpdateVisibility();
}

void SWeftViewport::UpdateVisibility()
{
	const bool bInteractive = Layers.ContainsByPredicate([](const FLayer& Layer) { return Layer.bInteractive; });
	SetVisibility(bInteractive ? EVisibility::Visible : EVisibility::HitTestInvisible);
}

FVector2D SWeftViewport::PixelToLogical(FVector2D WindowPixel) const
{
	return (WindowPixel - LastPosition) / LastScale;
}

bool SWeftViewport::IsPointerOverPage() const
{
	return Layers.ContainsByPredicate([](const FLayer& Layer) { return Layer.bPointerOver; });
}

bool SWeftViewport::IsPointerOverView(const UWeftView* InView) const
{
	return Layers.ContainsByPredicate([InView](const FLayer& Layer) { return Layer.bPointerOver && Layer.View.Get() == InView; });
}

void SWeftViewport::Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime)
{
	LastPosition = AllottedGeometry.GetAbsolutePosition();
	LastScale = AllottedGeometry.Scale;
	for (const FLayer& Layer : Layers)
	{
		if (UWeftView* Page = Layer.View.Get())
		{
			// CSS px are the widget's units divided by the page's content scale; device px Slate's.
			const float Content = Page->GetContentScale();
			Page->Resize(AllottedGeometry.GetLocalSize() / Content, AllottedGeometry.Scale * Content);
			Page->Advance();
		}
	}
}

int32 SWeftViewport::OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect,
	FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const
{
	if (!CVarWeftDraw.GetValueOnGameThread())
	{
		return LayerId;
	}
	// The engine's named textures, which every page may draw.
	TArray<TPair<FString, FTextureResource*>> Textures;
	if (const UWeftSubsystem* Weft = UWeftSubsystem::Get())
	{
		Weft->GetTextures(Textures);
	}
	// The widget in Slate's pixels: where each View's frame goes, at the View's device scale.
	const FVector2D Position = AllottedGeometry.GetAbsolutePosition();
	const FIntPoint Min(FMath::RoundToInt(Position.X), FMath::RoundToInt(Position.Y));
	int32 Layer = LayerId;
	for (const FLayer& Entry : Layers)
	{
		const UWeftView* Page = Entry.View.Get();
		TSharedPtr<FWeftFramePacket, ESPMode::ThreadSafe> Packet = Page && Entry.Drawer ? Page->MakePacket() : nullptr;
		if (!Packet)
		{
			continue;
		}
		const FVector2D Size = Page->GetLogicalSize() * Page->GetDeviceScale();
		const FIntRect Rect(Min, Min + FIntPoint(FMath::CeilToInt(Size.X), FMath::CeilToInt(Size.Y)));
		ENQUEUE_RENDER_COMMAND(WeftPending)(
			[Drawer = Entry.Drawer, Packet = MoveTemp(Packet), Rect, Textures](FRHICommandListImmediate&) mutable
			{
				// The resources' RHI textures, read where they are written: on the render thread.
				for (const TPair<FString, FTextureResource*>& Named : Textures)
				{
					if (FRHITexture* Texture = Named.Value->GetTextureRHI())
					{
						const FTCHARToUTF8 Utf8(*Named.Key);
						FWeftPacketTexture& Bound = Packet->Textures.AddDefaulted_GetRef();
						Bound.Name.Append(Utf8.Get(), Utf8.Length());
						Bound.Texture = Texture;
					}
				}
				Drawer->SetPending(MoveTemp(Packet), Rect);
			});
		FSlateDrawElement::MakeCustom(OutDrawElements, Layer, Entry.Drawer);
		++Layer;
	}
	return Layer;
}

uint32 SWeftViewport::Buttons(const FPointerEvent& MouseEvent)
{
	// MouseEvent.buttons: primary 1, secondary 2, middle 4.
	return (MouseEvent.IsMouseButtonDown(EKeys::LeftMouseButton) ? 1u : 0u) |
		(MouseEvent.IsMouseButtonDown(EKeys::RightMouseButton) ? 2u : 0u) |
		(MouseEvent.IsMouseButtonDown(EKeys::MiddleMouseButton) ? 4u : 0u);
}

FVector2D SWeftViewport::ToPage(const UWeftView* Page, FVector2D Local)
{
	return Page ? Local / Page->GetContentScale() : Local;
}

bool SWeftViewport::InteractiveCursor(const FString& Cursor)
{
	return !Cursor.IsEmpty() && Cursor != TEXT("auto") && Cursor != TEXT("default") && Cursor != TEXT("none");
}

bool SWeftViewport::TakesPointer(const UWeftView* Page, FVector2D Local, FString* OutCursor)
{
	FString Cursor;
	const bool bElement = Page && Page->HitTest(ToPage(Page, Local), Cursor);
	if (OutCursor)
	{
		*OutCursor = Cursor;
	}
	return bElement;
}

int32 SWeftViewport::LayerTakingPointer(FVector2D Local, FString* OutCursor, bool bWheel) const
{
	for (int32 I = Layers.Num() - 1; I >= 0; --I)
	{
		FString Cursor;
		if (Layers[I].bInteractive && TakesPointer(Layers[I].View.Get(), Local, &Cursor) &&
			(bWheel || Layers[I].Routing == EWeftPointerRouting::Element || InteractiveCursor(Cursor)))
		{
			if (OutCursor)
			{
				*OutCursor = Cursor;
			}
			return I;
		}
	}
	return INDEX_NONE;
}

FReply SWeftViewport::OnMouseMove(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	const FVector2D Local = MyGeometry.AbsoluteToLocal(MouseEvent.GetScreenSpacePosition());
	const uint32 Pressed = Buttons(MouseEvent);
	const double Now = FPlatformTime::Seconds();
	if (Local == LastMove && Pressed == LastMoveButtons && Now - LastMoveTime < StillMoveSeconds)
	{
		return HasMouseCapture() ? FReply::Handled() : FReply::Unhandled();
	}
	LastMove = Local;
	LastMoveButtons = Pressed;
	LastMoveTime = Now;
	const int32 Over = LayerTakingPointer(Local);
	for (int32 I = 0; I < Layers.Num(); ++I)
	{
		Layers[I].bPointerOver = I == Over;
		if (UWeftView* Page = Layers[I].View.Get())
		{
			Page->Pointer(EWeftPointer::Move, ToPage(Page, Local), Pressed);
		}
	}
	// The game sees the pointer move too, unless a page holds it.
	return HasMouseCapture() ? FReply::Handled() : FReply::Unhandled();
}

FReply SWeftViewport::OnMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	const FVector2D Local = MyGeometry.AbsoluteToLocal(MouseEvent.GetScreenSpacePosition());
	const int32 Taker = LayerTakingPointer(Local);
	UWeftView* Page = Layers.IsValidIndex(Taker) ? Layers[Taker].View.Get() : nullptr;
	if (!Page)
	{
		return FReply::Unhandled();
	}
	Captor = Page;
	Page->Pointer(EWeftPointer::Down, ToPage(Page, Local), Buttons(MouseEvent));
	return FReply::Handled().CaptureMouse(AsShared());
}

FReply SWeftViewport::OnMouseButtonUp(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	if (!HasMouseCapture())
	{
		return FReply::Unhandled();
	}
	if (UWeftView* Page = Captor.Get())
	{
		Page->Pointer(EWeftPointer::Up, ToPage(Page, MyGeometry.AbsoluteToLocal(MouseEvent.GetScreenSpacePosition())),
			Buttons(MouseEvent));
	}
	if (Buttons(MouseEvent))
	{
		return FReply::Handled();
	}
	Captor.Reset();
	return FReply::Handled().ReleaseMouseCapture();
}

FReply SWeftViewport::OnMouseWheel(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	const FVector2D Local = MyGeometry.AbsoluteToLocal(MouseEvent.GetScreenSpacePosition());
	FString Cursor;
	const int32 Taker = LayerTakingPointer(Local, &Cursor, /*bWheel*/ true);
	UWeftView* Page = Layers.IsValidIndex(Taker) ? Layers[Taker].View.Get() : nullptr;
	if (!Page)
	{
		return FReply::Unhandled();
	}
	// Slate's wheel is in notches, upward positive; the page's in px, downward positive (100 px a notch, as browsers).
	Page->Pointer(EWeftPointer::Wheel, ToPage(Page, Local), Buttons(MouseEvent), FVector2D(0, -MouseEvent.GetWheelDelta() * 100.0));
	// A page routed by its cursor shares the wheel with the game under the arrow.
	return Layers[Taker].Routing == EWeftPointerRouting::Cursor && !InteractiveCursor(Cursor) ? FReply::Unhandled()
																							   : FReply::Handled();
}

void SWeftViewport::OnMouseLeave(const FPointerEvent& MouseEvent)
{
	LastMove = FVector2D(-1, -1);
	for (FLayer& Layer : Layers)
	{
		Layer.bPointerOver = false;
		if (UWeftView* Page = Layer.View.Get())
		{
			Page->Pointer(EWeftPointer::Leave, FVector2D::ZeroVector, 0);
		}
	}
}

FCursorReply SWeftViewport::OnCursorQuery(const FGeometry& MyGeometry, const FPointerEvent& CursorEvent) const
{
	FString Cursor;
	if (LayerTakingPointer(MyGeometry.AbsoluteToLocal(CursorEvent.GetScreenSpacePosition()), &Cursor) == INDEX_NONE)
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
