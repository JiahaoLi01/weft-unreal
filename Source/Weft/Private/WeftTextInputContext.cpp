#include "WeftTextInputContext.h"

#include "Framework/Application/SlateApplication.h"
#include "Widgets/SWindow.h"
#include "WeftView.h"

void FWeftTextInputContext::SetTarget(UWeftView* InPage, const FGeometry& InGeometry)
{
	Page = InPage;
	Geometry = InGeometry;
}

void FWeftTextInputContext::Flush(ITextInputMethodChangeNotifier* Notifier)
{
	UWeftView* Target = Page.Get();
	if (bDiscarding)
	{
		// Nothing of the composition the page ended goes to it.
		if (bComposing)
		{
			return;
		}
		bDiscarding = false;
		Given = Text.Len();
	}
	if (bComposing)
	{
		// The composition is the range the input method gave, or without one (Microsoft Pinyin through Unreal's TSF store
		// gives none) the text it put in since the page's text. Text before a range (the result of a composition that
		// ended, as IMM begins the next in the same message) is committed first, ending the page's composition with it.
		const int32 Begin = bRanged ? FMath::Min(int32(CompositionBegin), Text.Len()) : Given;
		if (Begin > Given)
		{
			if (Target)
			{
				Target->CommitText(Text.Mid(Given, Begin - Given));
			}
			Given = Begin;
			Sent.Reset();
			SentCursor = 0;
		}
		const FString Composition = bRanged ? Text.Mid(CompositionBegin, CompositionLength) : Text.Mid(Given);
		const int32 Focus = int32(SelectionBegin + (Caret == ECaretPosition::Ending ? SelectionLength : 0));
		const int32 Cursor = FMath::Clamp(Focus - Begin, 0, Composition.Len());
		if (Composition != Sent || Cursor != SentCursor)
		{
			// An empty composition cancels the page's.
			if (Target && (!Composition.IsEmpty() || !Sent.IsEmpty()))
			{
				Target->Compose(Composition, Cursor);
			}
			Sent = Composition;
			SentCursor = Cursor;
		}
		return;
	}
	// No composition goes on: the text past what the page has is committed (ending the page's composition with it), or
	// the page's composition is cancelled.
	const FString Committed = Text.Mid(Given);
	if (Target && !Committed.IsEmpty())
	{
		Target->CommitText(Committed);
	}
	else if (Target && !Sent.IsEmpty())
	{
		Target->Compose(FString());
	}
	Sent.Reset();
	SentCursor = 0;
	bComposing = false;
	// The document empties, the input method told outside its edit.
	const uint32 Length = uint32(Text.Len());
	Text.Reset();
	SelectionBegin = SelectionLength = 0;
	CompositionBegin = CompositionLength = 0;
	Given = 0;
	if (Length && Notifier)
	{
		Notifier->NotifyTextChanged(0, Length, 0);
		Notifier->NotifySelectionChanged();
	}
}

void FWeftTextInputContext::Discard()
{
	bDiscarding = bComposing;
	Given = Text.Len();
	Sent.Reset();
	SentCursor = 0;
}

void FWeftTextInputContext::Reset()
{
	Text.Reset();
	SelectionBegin = SelectionLength = 0;
	Caret = ECaretPosition::Ending;
	bComposing = bRanged = bDiscarding = false;
	CompositionBegin = CompositionLength = 0;
	Given = 0;
	Sent.Reset();
	SentCursor = 0;
}

void FWeftTextInputContext::GetSelectionRange(uint32& OutBeginIndex, uint32& OutLength, ECaretPosition& OutCaretPosition)
{
	OutBeginIndex = SelectionBegin;
	OutLength = SelectionLength;
	OutCaretPosition = Caret;
}

void FWeftTextInputContext::SetSelectionRange(const uint32 InBeginIndex, const uint32 InLength, const ECaretPosition InCaretPosition)
{
	const uint32 Length = uint32(Text.Len());
	SelectionBegin = FMath::Min(InBeginIndex, Length);
	SelectionLength = FMath::Min(InLength, Length - SelectionBegin);
	Caret = InCaretPosition;
}

void FWeftTextInputContext::GetTextInRange(const uint32 InBeginIndex, const uint32 InLength, FString& OutString)
{
	OutString = Text.Mid(InBeginIndex, InLength);
}

void FWeftTextInputContext::SetTextInRange(const uint32 InBeginIndex, const uint32 InLength, const FString& InString)
{
	const uint32 Length = uint32(Text.Len());
	const uint32 Begin = FMath::Min(InBeginIndex, Length);
	const uint32 Replaced = FMath::Min(InLength, Length - Begin);
	Text = Text.Left(Begin) + InString + Text.Mid(Begin + Replaced);
	// An edit of the whole composition (an IMM result) is the composition; IMM gives a composition's new range before its
	// text, TSF after it.
	if (bComposing && bRanged && Begin == CompositionBegin && Replaced == CompositionLength)
	{
		CompositionLength = uint32(InString.Len());
	}
	// The input method edits no text the page has.
	Given = FMath::Min(Given, int32(Begin));
	SetSelectionRange(SelectionBegin, SelectionLength, Caret);
}

bool FWeftTextInputContext::GetTextBounds(const uint32 InBeginIndex, const uint32 InLength, FVector2D& OutPosition, FVector2D& OutSize)
{
	// The candidate window goes below the page's caret, whatever range the input method measures.
	const UWeftView* Target = Page.Get();
	ToScreen(Target ? Target->GetTextInput().Caret : FBox2D(ForceInit), OutPosition, OutSize);
	return false;
}

void FWeftTextInputContext::GetScreenBounds(FVector2D& OutPosition, FVector2D& OutSize)
{
	const UWeftView* Target = Page.Get();
	ToScreen(Target ? Target->GetTextInput().Field : FBox2D(ForceInit), OutPosition, OutSize);
}

void FWeftTextInputContext::ToScreen(const FBox2D& Box, FVector2D& OutPosition, FVector2D& OutSize) const
{
	// The page's logical px are the widget's units divided by its content scale.
	const UWeftView* Target = Page.Get();
	const float Content = Target ? Target->GetContentScale() : 1.f;
	OutPosition = Geometry.LocalToAbsolute(Box.Min * Content);
	OutSize = Geometry.LocalToAbsolute(Box.Max * Content) - OutPosition;
}

TSharedPtr<FGenericWindow> FWeftTextInputContext::GetWindow()
{
	const TSharedPtr<SWidget> Widget = Owner.Pin();
	if (!Widget || !FSlateApplication::IsInitialized())
	{
		return nullptr;
	}
	const TSharedPtr<SWindow> Window = FSlateApplication::Get().FindWidgetWindow(Widget.ToSharedRef());
	return Window ? Window->GetNativeWindow() : nullptr;
}

void FWeftTextInputContext::BeginComposition()
{
	if (bComposing)
	{
		return;
	}
	bComposing = true;
	bRanged = false;
	CompositionBegin = SelectionBegin;
	CompositionLength = 0;
}

void FWeftTextInputContext::UpdateCompositionRange(const int32 InBeginIndex, const uint32 InLength)
{
	const uint32 Length = uint32(Text.Len());
	bRanged = true;
	CompositionBegin = FMath::Min(uint32(FMath::Max(InBeginIndex, 0)), Length);
	CompositionLength = InLength;
}

void FWeftTextInputContext::EndComposition()
{
	bComposing = false;
}
