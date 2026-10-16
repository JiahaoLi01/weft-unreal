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
}

void SWeftViewport::Construct(const FArguments& InArgs)
{
	// The pages move every frame (their anchors at least): paint every frame.
	SetCanTick(true);
	ForceVolatile(true);
	if (InArgs._View)
	{
		AddView(InArgs._View, 0, InArgs._Interactive);
	}
	UpdateVisibility();
}

void SWeftViewport::AddView(UWeftView* InView, int32 Order, bool bInteractive)
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
			Page->Resize(AllottedGeometry.GetLocalSize(), AllottedGeometry.Scale);
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
		const FVector2D Size = AllottedGeometry.GetLocalSize() * Page->GetDeviceScale();
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

bool SWeftViewport::TakesPointer(const UWeftView* Page, FVector2D Local, FString* OutCursor)
{
	FString Cursor;
	const bool bElement = Page && Page->HitTest(Local, Cursor);
	if (OutCursor)
	{
		*OutCursor = Cursor;
	}
	return bElement;
}

int32 SWeftViewport::LayerTakingPointer(FVector2D Local, FString* OutCursor) const
{
	for (int32 I = Layers.Num() - 1; I >= 0; --I)
	{
		if (Layers[I].bInteractive && TakesPointer(Layers[I].View.Get(), Local, OutCursor))
		{
			return I;
		}
	}
	return INDEX_NONE;
}

FReply SWeftViewport::OnMouseMove(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	const FVector2D Local = MyGeometry.AbsoluteToLocal(MouseEvent.GetScreenSpacePosition());
	const int32 Over = LayerTakingPointer(Local);
	for (int32 I = 0; I < Layers.Num(); ++I)
	{
		Layers[I].bPointerOver = I == Over;
		if (UWeftView* Page = Layers[I].View.Get())
		{
			Page->Pointer(EWeftPointer::Move, Local, Buttons(MouseEvent));
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
	Page->Pointer(EWeftPointer::Down, Local, Buttons(MouseEvent));
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
		Page->Pointer(EWeftPointer::Up, MyGeometry.AbsoluteToLocal(MouseEvent.GetScreenSpacePosition()), Buttons(MouseEvent));
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
	const int32 Taker = LayerTakingPointer(Local);
	UWeftView* Page = Layers.IsValidIndex(Taker) ? Layers[Taker].View.Get() : nullptr;
	if (!Page)
	{
		return FReply::Unhandled();
	}
	// Slate's wheel is in notches, upward positive; the page's in px, downward positive (100 px a notch, as browsers).
	Page->Pointer(EWeftPointer::Wheel, Local, Buttons(MouseEvent), FVector2D(0, -MouseEvent.GetWheelDelta() * 100.0));
	return FReply::Handled();
}

void SWeftViewport::OnMouseLeave(const FPointerEvent& MouseEvent)
{
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
