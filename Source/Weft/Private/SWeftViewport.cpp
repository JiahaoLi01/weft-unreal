#include "SWeftViewport.h"

#include "CoreGlobals.h"
#include "Framework/Application/SlateApplication.h"
#include "GenericPlatform/ITextInputMethodSystem.h"
#include "HAL/IConsoleManager.h"
#include "Render/WeftFramePacket.h"
#include "Render/WeftSlateDrawer.h"
#include "RenderingThread.h"
#include "Rendering/DrawElements.h"
#include "TextureResource.h"
#include "WeftSubsystem.h"
#include "WeftTextInputContext.h"
#include "WeftView.h"

namespace
{
	TAutoConsoleVariable<int32> CVarWeftDraw(TEXT("weft.Draw"), 1,
		TEXT("Draw weft views (0: their pages still run, nothing is drawn)."), ECVF_Default);

	// How often a page takes a move of a pointer that stands still (Slate sends one as widgets move under it): each
	// is a hit test of the page's latest frame.
	constexpr double StillMoveSeconds = 0.1;

	// The modifier keys of a pointer event (Shift extends a text field's selection to a press).
	EWeftModifier PointerModifiers(const FPointerEvent& MouseEvent)
	{
		return (MouseEvent.IsShiftDown() ? EWeftModifier::Shift : EWeftModifier::None) |
			(MouseEvent.IsControlDown() ? EWeftModifier::Control : EWeftModifier::None) |
			(MouseEvent.IsAltDown() ? EWeftModifier::Alt : EWeftModifier::None) |
			(MouseEvent.IsCommandDown() ? EWeftModifier::Meta : EWeftModifier::None);
	}

	ITextInputMethodSystem* InputMethodSystem()
	{
		return FSlateApplication::IsInitialized() ? FSlateApplication::Get().GetTextInputMethodSystem() : nullptr;
	}

	// Slate's batcher keeps a plain pointer to a custom element's drawer (FSlateElementBatcher::AddCustomElement pins the
	// element's weak pointer only while it batches), and the render thread draws with it after the game thread has gone
	// on. A drawer let go of on the game thread (its layer removed, its widget gone) can be gone by the time the frame
	// Slate batched last is drawn, which then draws with freed memory. So a layer's drawer is let go of on the render
	// thread, after every draw enqueued before it.
	void ReleaseDrawerOnRenderThread(TSharedPtr<FWeftSlateDrawer, ESPMode::ThreadSafe>& Drawer)
	{
		if (Drawer)
		{
			ENQUEUE_RENDER_COMMAND(WeftReleaseDrawer)([Released = MoveTemp(Drawer)](FRHICommandListImmediate&) mutable
			{
				Released.Reset();
			});
		}
	}
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

SWeftViewport::~SWeftViewport()
{
	for (FLayer& Layer : Layers)
	{
		ReleaseDrawerOnRenderThread(Layer.Drawer);
	}
	if (TextInput && TextInputNotifier)
	{
		if (ITextInputMethodSystem* System = InputMethodSystem())
		{
			const TSharedRef<ITextInputMethodContext> Context = TextInput.ToSharedRef();
			if (bTextInputActive && System->IsActiveContext(Context))
			{
				System->DeactivateContext(Context);
			}
			System->UnregisterContext(Context);
		}
	}
}

void SWeftViewport::AddView(UWeftView* InView, int32 Order, bool bInteractive, EWeftPointerRouting Routing)
{
	if (!InView)
	{
		return;
	}
	// A view added again keeps what its page was told of the focus.
	const FLayer* Before = Layers.FindByPredicate([InView](const FLayer& Other) { return Other.View.Get() == InView; });
	const bool bPageFocus = Before ? Before->bPageFocus : true;
	RemoveView(InView);
	FLayer Layer;
	Layer.View = InView;
	Layer.bPageFocus = bPageFocus;
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
	Route.bValid = false;
	UpdateVisibility();
	ApplyFocus();
}

void SWeftViewport::RemoveView(const UWeftView* InView)
{
	const auto Removed = [InView](const FLayer& Layer) { return Layer.View.Get() == InView || !Layer.View.IsValid(); };
	for (FLayer& Layer : Layers)
	{
		if (Removed(Layer))
		{
			ReleaseDrawerOnRenderThread(Layer.Drawer);
		}
	}
	Layers.RemoveAll(Removed);
	if (Captor.Get() == InView)
	{
		Captor.Reset();
	}
	Route.bValid = false;
	UpdateVisibility();
}

void SWeftViewport::FocusView(UWeftView* InView)
{
	KeyboardView = InView;
	bFocusPending = true;
	if (FSlateApplication::IsInitialized())
	{
		FSlateApplication::Get().SetAllUserFocus(AsShared(), EFocusCause::SetDirectly);
	}
	ApplyFocus();
}

UWeftView* SWeftViewport::GetKeyboardView() const
{
	const UWeftView* Chosen = KeyboardView.Get();
	UWeftView* Topmost = nullptr;
	for (const FLayer& Layer : Layers)
	{
		if (Layer.bInteractive && Layer.View.IsValid())
		{
			if (Layer.View.Get() == Chosen)
			{
				return Layer.View.Get();
			}
			Topmost = Layer.View.Get();
		}
	}
	return Topmost;
}

void SWeftViewport::ApplyFocus(bool bPressing)
{
	const bool bActive = (bKeyboardFocus || bPressing) &&
		(!FSlateApplication::IsInitialized() || FSlateApplication::Get().IsActive());
	const UWeftView* Keyboard = GetKeyboardView();
	for (FLayer& Layer : Layers)
	{
		UWeftView* Page = Layer.View.Get();
		const bool bFocus = bActive && Page && Page == Keyboard;
		if (Page && Layer.bPageFocus != bFocus)
		{
			Layer.bPageFocus = bFocus;
			Page->SetFocus(bFocus);
		}
	}
}

bool SWeftViewport::SupportsKeyboardFocus() const
{
	return Layers.ContainsByPredicate([](const FLayer& Layer) { return Layer.bInteractive; });
}

FReply SWeftViewport::OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent)
{
	UWeftView* Page = GetKeyboardView();
	if (!Page)
	{
		return FReply::Unhandled();
	}
	const bool bUsed = Page->Key(InKeyEvent, true) != EWeftInput::None;
	// What the input method did with its key follows the key (TSF's edit comes before the key reaches the widget).
	FlushTextInput(MyGeometry);
	return bUsed || InKeyEvent.GetKeyCode() == FWeftTextInputContext::ProcessKey ? FReply::Handled() : FReply::Unhandled();
}

FReply SWeftViewport::OnKeyUp(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent)
{
	FlushTextInput(MyGeometry);
	UWeftView* Page = GetKeyboardView();
	return Page && Page->Key(InKeyEvent, false) != EWeftInput::None ? FReply::Handled() : FReply::Unhandled();
}

FReply SWeftViewport::OnKeyChar(const FGeometry& MyGeometry, const FCharacterEvent& InCharacterEvent)
{
	FlushTextInput(MyGeometry);
	UWeftView* Page = GetKeyboardView();
	if (!Page)
	{
		return FReply::Unhandled();
	}
	// A character outside the Basic Multilingual Plane comes as its two surrogates.
	const TCHAR Character = InCharacterEvent.GetCharacter();
	if (Character >= 0xD800 && Character <= 0xDBFF)
	{
		HighSurrogate = Character;
		return FReply::Handled();
	}
	FString Typed;
	if (Character >= 0xDC00 && Character <= 0xDFFF && HighSurrogate)
	{
		Typed.AppendChar(HighSurrogate);
	}
	HighSurrogate = 0;
	Typed.AppendChar(Character);
	return Page->Text(Typed) != EWeftInput::None ? FReply::Handled() : FReply::Unhandled();
}

FReply SWeftViewport::OnFocusReceived(const FGeometry& MyGeometry, const FFocusEvent& InFocusEvent)
{
	bKeyboardFocus = true;
	ApplyFocus();
	return FReply::Handled();
}

FNavigationReply SWeftViewport::OnNavigation(const FGeometry& MyGeometry, const FNavigationEvent& InNavigationEvent)
{
	return FNavigationReply::Stop();
}

void SWeftViewport::OnFocusLost(const FFocusEvent& InFocusEvent)
{
	bKeyboardFocus = false;
	HighSurrogate = 0;
	ApplyFocus();
	// Before the widget that takes the focus activates a context of its own.
	DeactivateTextInput();
}

TSharedRef<ITextInputMethodContext> SWeftViewport::GetTextInputMethodContext()
{
	if (!TextInput)
	{
		TextInput = MakeShared<FWeftTextInputContext>(SharedThis(this));
	}
	return TextInput.ToSharedRef();
}

void SWeftViewport::FlushTextInput(const FGeometry& Geometry)
{
	if (TextInput)
	{
		TextInput->SetTarget(GetKeyboardView(), Geometry);
		TextInput->Flush(TextInputNotifier.Get());
	}
}

void SWeftViewport::DeactivateTextInput()
{
	if (!TextInput || !bTextInputActive)
	{
		return;
	}
	bTextInputActive = false;
	TextInput->Discard();
	ITextInputMethodSystem* System = InputMethodSystem();
	const TSharedRef<ITextInputMethodContext> Context = TextInput.ToSharedRef();
	// Another widget's context may be the active one by now.
	if (System && System->IsActiveContext(Context))
	{
		System->DeactivateContext(Context);
	}
}

void SWeftViewport::UpdateTextInput(const FGeometry& Geometry)
{
	UWeftView* Page = GetKeyboardView();
	const bool bFocused = bKeyboardFocus && (!FSlateApplication::IsInitialized() || FSlateApplication::Get().IsActive());
	if (!TextInput && !(bFocused && Page))
	{
		return;
	}
	const TSharedRef<ITextInputMethodContext> Context = GetTextInputMethodContext();
	FlushTextInput(Geometry);
	const FWeftTextInput Input = bFocused && Page ? Page->GetTextInput() : FWeftTextInput();
	// The page ended the composition (a script set the value, or the field lost the focus): the input method's ends too,
	// its text dropped (turning the input method off and on ends it).
	const bool bEnded = TextInput->HasComposition() && !Input.bComposing;
	if (bEnded)
	{
		TextInput->Discard();
	}
	if (bEnded || !Input.bActive || Input.bPassword)
	{
		DeactivateTextInput();
	}
	ITextInputMethodSystem* System = InputMethodSystem();
	if (!System || !Input.bActive || Input.bPassword || bTextInputActive || !TextInput->GetWindow())
	{
		// The candidate window follows the caret.
		if (bTextInputActive && TextInputNotifier &&
			(Input.Caret.Min != TextInputCaret.Min || Input.Caret.Max != TextInputCaret.Max))
		{
			TextInputCaret = Input.Caret;
			TextInputNotifier->NotifyLayoutChanged(ITextInputMethodChangeNotifier::ELayoutChangeType::Changed);
		}
		return;
	}
	if (!TextInputNotifier)
	{
		TextInputNotifier = System->RegisterContext(Context);
	}
	// A fresh document: a composition the input method never ended is gone with its last activation.
	TextInput->Reset();
	System->ActivateContext(Context);
	bTextInputActive = true;
	TextInputCaret = Input.Caret;
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
	// The keyboard focus as Slate has it now, and the application's (a window of another application took it). A packaged
	// game gives the game viewport the focus when its window first shows it, after the first level's BeginPlay: the
	// focus FocusView asked for is taken again until the widget has it.
	bKeyboardFocus = HasKeyboardFocus();
	if (bFocusPending && !bKeyboardFocus && FSlateApplication::IsInitialized())
	{
		FSlateApplication::Get().SetAllUserFocus(AsShared(), EFocusCause::SetDirectly);
		bKeyboardFocus = HasKeyboardFocus();
	}
	bFocusPending = bFocusPending && !bKeyboardFocus;
	ApplyFocus();
	for (const FLayer& Layer : Layers)
	{
		if (UWeftView* Page = Layer.View.Get())
		{
			// CSS px are the widget's units divided by the page's content scale; device px Slate's.
			const float Content = Page->GetContentScale();
			Page->Resize(AllottedGeometry.GetLocalSize() / Content, AllottedGeometry.Scale * Content);
		}
	}
	// The frame's pointer, where it was last, and the input method's text, then the pages' frame.
	FlushPointer();
	UpdateTextInput(AllottedGeometry);
	for (const FLayer& Layer : Layers)
	{
		if (UWeftView* Page = Layer.View.Get())
		{
			Page->Advance();
		}
	}
	Route.bValid = false;
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

int32 SWeftViewport::RouteAt(FVector2D Local, FString* OutCursor) const
{
	if (!Route.bValid || Route.Local != Local)
	{
		Route.Layer = LayerTakingPointer(Local, &Route.Cursor);
		Route.Local = Local;
		Route.bValid = true;
	}
	if (OutCursor)
	{
		*OutCursor = Route.Cursor;
	}
	return Route.Layer;
}

void SWeftViewport::SetPointerOver(int32 Over)
{
	for (int32 I = 0; I < Layers.Num(); ++I)
	{
		Layers[I].bPointerOver = I == Over;
	}
}

void SWeftViewport::FlushPointer()
{
	if (!bPointerPending)
	{
		return;
	}
	bPointerPending = false;
	SetPointerOver(RouteAt(PendingLocal));
	for (int32 I = 0; I < Layers.Num(); ++I)
	{
		if (UWeftView* Page = Layers[I].View.Get())
		{
			Page->Pointer(EWeftPointer::Move, ToPage(Page, PendingLocal), PendingButtons, FVector2D::ZeroVector,
				EWeftModifier(PendingModifiers));
		}
	}
	LastMove = PendingLocal;
	LastMoveButtons = PendingButtons;
	LastMoveTime = FPlatformTime::Seconds();
	// The pages' handlers may have changed what is under the pointer.
	Route.bValid = false;
}

FReply SWeftViewport::OnMouseMove(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	const FVector2D Local = MyGeometry.AbsoluteToLocal(MouseEvent.GetScreenSpacePosition());
	const uint32 Pressed = Buttons(MouseEvent);
	// A move where the pointer stands (Slate repeats one while widgets move under it) at most every StillMoveSeconds
	// after the pages took one there.
	if (bPointerPending || Local != LastMove || Pressed != LastMoveButtons ||
		FPlatformTime::Seconds() - LastMoveTime >= StillMoveSeconds)
	{
		// Only kept: the pages get it in the frame's Tick (or before a button, the wheel or a leave).
		PendingLocal = Local;
		PendingButtons = Pressed;
		PendingModifiers = uint32(PointerModifiers(MouseEvent));
		bPointerPending = true;
		// Which layer the pointer is over, for the game this frame: at the frame's first move.
		if (RoutedFrame != GFrameCounter)
		{
			RoutedFrame = GFrameCounter;
			SetPointerOver(RouteAt(PendingLocal));
		}
	}
	// The game sees the pointer move too, unless a page holds it.
	return HasMouseCapture() ? FReply::Handled() : FReply::Unhandled();
}

FReply SWeftViewport::OnMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	// The pages see the pointer where it is before the press (what it is over, then the button).
	FlushPointer();
	const FVector2D Local = MyGeometry.AbsoluteToLocal(MouseEvent.GetScreenSpacePosition());
	const int32 Taker = RouteAt(Local);
	UWeftView* Page = Layers.IsValidIndex(Taker) ? Layers[Taker].View.Get() : nullptr;
	if (!Page)
	{
		return FReply::Unhandled();
	}
	Captor = Page;
	// The press gives the layer the keyboard and the widget Slate's focus: the page has it before its mousedown, as a
	// browser window takes the focus before the press that gave it.
	KeyboardView = Page;
	ApplyFocus(/*bPressing*/ true);
	Page->Pointer(EWeftPointer::Down, ToPage(Page, Local), Buttons(MouseEvent), FVector2D::ZeroVector, PointerModifiers(MouseEvent));
	Route.bValid = false;
	return FReply::Handled().CaptureMouse(AsShared()).SetUserFocus(AsShared(), EFocusCause::Mouse);
}

FReply SWeftViewport::OnMouseButtonUp(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	if (!HasMouseCapture())
	{
		return FReply::Unhandled();
	}
	FlushPointer();
	if (UWeftView* Page = Captor.Get())
	{
		Page->Pointer(EWeftPointer::Up, ToPage(Page, MyGeometry.AbsoluteToLocal(MouseEvent.GetScreenSpacePosition())),
			Buttons(MouseEvent), FVector2D::ZeroVector, PointerModifiers(MouseEvent));
		Route.bValid = false;
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
	FlushPointer();
	const FVector2D Local = MyGeometry.AbsoluteToLocal(MouseEvent.GetScreenSpacePosition());
	FString Cursor;
	const int32 Taker = LayerTakingPointer(Local, &Cursor, /*bWheel*/ true);
	UWeftView* Page = Layers.IsValidIndex(Taker) ? Layers[Taker].View.Get() : nullptr;
	if (!Page)
	{
		return FReply::Unhandled();
	}
	// Slate's wheel is in notches, upward positive; the page's in px, downward positive (100 px a notch, as browsers).
	Page->Pointer(EWeftPointer::Wheel, ToPage(Page, Local), Buttons(MouseEvent), FVector2D(0, -MouseEvent.GetWheelDelta() * 100.0),
		PointerModifiers(MouseEvent));
	Route.bValid = false;
	// A page routed by its cursor shares the wheel with the game under the arrow.
	return Layers[Taker].Routing == EWeftPointerRouting::Cursor && !InteractiveCursor(Cursor) ? FReply::Unhandled()
																							   : FReply::Handled();
}

void SWeftViewport::OnMouseLeave(const FPointerEvent& MouseEvent)
{
	bPointerPending = false;
	LastMove = FVector2D(-1, -1);
	for (FLayer& Layer : Layers)
	{
		Layer.bPointerOver = false;
		if (UWeftView* Page = Layer.View.Get())
		{
			Page->Pointer(EWeftPointer::Leave, FVector2D::ZeroVector, 0);
		}
	}
	Route.bValid = false;
}

FCursorReply SWeftViewport::OnCursorQuery(const FGeometry& MyGeometry, const FPointerEvent& CursorEvent) const
{
	FString Cursor;
	if (RouteAt(MyGeometry.AbsoluteToLocal(CursorEvent.GetScreenSpacePosition()), &Cursor) == INDEX_NONE)
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
