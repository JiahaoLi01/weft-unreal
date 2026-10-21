#pragma once

#include "CoreMinimal.h"
#include "UObject/WeakObjectPtrTemplates.h"
#include "Widgets/SLeafWidget.h"

class FWeftSlateDrawer;
class FWeftTextInputContext;
class ITextInputMethodChangeNotifier;
class ITextInputMethodContext;
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
 *
 * The pages get the pointer's moves once a frame, where it was last (as browsers align them with animation frames), and
 * before a button, the wheel or a leave: Slate hands over every move the platform queued, many in a slow frame, and each
 * costs a page a hit test and its handlers, which made a slow frame slower the more the pointer moved. A move where the
 * pointer stands, which Slate repeats while widgets move under it, they get at most every 0.1 s.
 *
 * The keyboard goes to one layer, as a browser window's to its page: the one a press last went to, or FocusView's, else
 * the topmost interactive layer. A press on a page takes Slate's keyboard focus; while the widget has it (and the
 * application is active) that layer's page has the focus (window focus, :focus), the other pages not. Its keys go to the
 * page's focused element with Chrome's default actions; a key the page does not use is unhandled, so it goes on to the
 * game (under a game viewport, its input), as Chrome gives the browser the keys a page did not use. A key that types into
 * a text field is the page's.
 *
 * The platform's input method (Slate's ITextInputMethodSystem: IMM or TSF on Windows) is on while that page's focused
 * text field takes text (not a password field's), its candidate window below the field's caret: what it composes goes
 * to the field as the page's composition, the text it commits as a commit, and the keys it takes as "Process" keys.
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
	virtual ~SWeftViewport();

	/** A view as a layer of the given order (a view added again changes its order, interactivity and routing). The view
	 *  lays out in the widget's units divided by its content scale (UWeftView::SetContentScale). */
	void AddView(UWeftView* InView, int32 Order = 0, bool bInteractive = true,
		EWeftPointerRouting Routing = EWeftPointerRouting::Element);
	void RemoveView(const UWeftView* InView);
	int32 NumViews() const { return Layers.Num(); }

	/** Gives the view's layer the keyboard, and the widget Slate's keyboard focus for every user (the game takes it back
	 *  with FSlateApplication::SetAllUserFocusToGameViewport, or a press on it). Asked before the widget is in a window,
	 *  or before the engine has started (which gives the game viewport the focus as its window shows it), the widget
	 *  takes the focus at its first frame. */
	void FocusView(UWeftView* InView);
	/** The view whose page gets the keys while the widget has the keyboard focus: the one a press or FocusView gave it
	 *  last, else the topmost interactive layer's; null without an interactive layer. */
	UWeftView* GetKeyboardView() const;

	/** The input method's context at the keyboard layer's page: Slate's input method system edits what it composes in
	 *  it while that page's text field takes text. */
	TSharedRef<ITextInputMethodContext> GetTextInputMethodContext();

	/** The View's logical px of a point in the widget's window, in Slate's pixels (a viewport's own pixels for a widget
	 *  that covers a game viewport); the widget's last geometry. */
	FVector2D PixelToLogical(FVector2D WindowPixel) const;

	/** Whether the pointer was over an element of an interactive layer's page that takes it, at its first move of the
	 *  frame or its last delivered one (any layer's, or one view's): the game may ignore the pointer then, as it gets no
	 *  clicks there. */
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

	virtual bool SupportsKeyboardFocus() const override;
	/** None: the pages show their focus themselves (:focus-visible), not Slate's rectangle round the whole widget. */
	virtual const FSlateBrush* GetFocusBrush() const override { return nullptr; }
	virtual FReply OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent) override;
	virtual FReply OnKeyUp(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent) override;
	virtual FReply OnKeyChar(const FGeometry& MyGeometry, const FCharacterEvent& InCharacterEvent) override;
	virtual FReply OnFocusReceived(const FGeometry& MyGeometry, const FFocusEvent& InFocusEvent) override;
	virtual void OnFocusLost(const FFocusEvent& InFocusEvent) override;

private:
	struct FLayer
	{
		TWeakObjectPtr<UWeftView> View;
		TSharedPtr<FWeftSlateDrawer, ESPMode::ThreadSafe> Drawer;
		int32 Order = 0;
		bool bInteractive = true;
		EWeftPointerRouting Routing = EWeftPointerRouting::Element;
		bool bPointerOver = false;
		/** Whether the page was last told it has the keyboard focus (weft's Views start with it). */
		bool bPageFocus = true;
	};

	/** The topmost interactive layer whose page takes the pointer at a point of the widget (its units), and its cursor;
	 *  INDEX_NONE for none. bWheel: where the page has an element at all (the wheel reaches it whatever its routing). */
	int32 LayerTakingPointer(FVector2D Local, FString* OutCursor = nullptr, bool bWheel = false) const;
	/** LayerTakingPointer (not the wheel's), remembered for its point until a page may have changed: a page's pointer
	 *  handlers, its frame or the layers. A hit test walks a page's scene. */
	int32 RouteAt(FVector2D Local, FString* OutCursor = nullptr) const;
	/** Marks the layer the pointer is over (INDEX_NONE: none). */
	void SetPointerOver(int32 Over);
	/** The pointer's pending move to every layer's page, if it moved since the pages last saw it. */
	void FlushPointer();
	static bool TakesPointer(const UWeftView* Page, FVector2D Local, FString* OutCursor = nullptr);
	/** Whether a cursor of the page says something there takes clicks: not the default arrow. */
	static bool InteractiveCursor(const FString& Cursor);
	/** A point of the widget (its units) in a page's logical px. */
	static FVector2D ToPage(const UWeftView* Page, FVector2D Local);
	/** Visible while a layer is interactive, else invisible to the pointer. */
	void UpdateVisibility();
	/** Tells each layer's page whether it has the keyboard focus: the keyboard layer's has it while the widget has
	 *  Slate's (or a press is giving it) and the application is active. */
	void ApplyFocus(bool bPressing = false);
	static uint32 Buttons(const FPointerEvent& MouseEvent);
	/** Turns the input method on (activates the context) while the keyboard layer's page has a text field that takes
	 *  text (not a password field) and the widget has the keyboard, off otherwise, after giving the page what it did;
	 *  drops its composition when the page ended it; tells it when the caret moved. */
	void UpdateTextInput(const FGeometry& Geometry);
	/** Gives the keyboard layer's page what the input method did since the last time. */
	void FlushTextInput(const FGeometry& Geometry);
	/** The input method is off for the widget: its composition, which the page ended, is dropped. */
	void DeactivateTextInput();

	/** By order, lowest first. */
	TArray<FLayer> Layers;
	/** The view that took a button down, which gets the pointer until every button is up. */
	TWeakObjectPtr<UWeftView> Captor;
	/** The view a press or FocusView gave the keyboard; whether the widget has Slate's keyboard focus; a high surrogate
	 *  of OnKeyChar, waiting for its low one. */
	TWeakObjectPtr<UWeftView> KeyboardView;
	bool bKeyboardFocus = false;
	/** FocusView's focus, asked again each frame until the widget has it. */
	bool bFocusPending = false;
	TCHAR HighSurrogate = 0;
	/** The input method's context (made at the first frame with the keyboard), its notifier once registered with Slate's
	 *  input method system, whether it is the active context there, and the caret it was last told of. */
	TSharedPtr<FWeftTextInputContext> TextInput;
	TSharedPtr<ITextInputMethodChangeNotifier> TextInputNotifier;
	bool bTextInputActive = false;
	FBox2D TextInputCaret = FBox2D(ForceInit);
	FVector2D LastPosition = FVector2D::ZeroVector;
	float LastScale = 1.f;

	/** The pointer's last move (the widget's units), its buttons and the modifier keys held, not yet delivered to the
	 *  pages. */
	bool bPointerPending = false;
	FVector2D PendingLocal = FVector2D::ZeroVector;
	uint32 PendingButtons = 0;
	uint32 PendingModifiers = 0;
	/** The frame (GFrameCounter) whose first move marked the layer the pointer is over. */
	uint64 RoutedFrame = MAX_uint64;
	/** The last move the pages took: Slate repeats a move where the pointer stands while widgets move under it, which
	 *  the pages take at most every StillMoveSeconds (their hover follows what scrolls under a still pointer). */
	FVector2D LastMove = FVector2D(-1, -1);
	uint32 LastMoveButtons = 0;
	double LastMoveTime = 0;

	struct FRoute
	{
		bool bValid = false;
		FVector2D Local = FVector2D::ZeroVector;
		int32 Layer = INDEX_NONE;
		FString Cursor;
	};
	mutable FRoute Route;
};
