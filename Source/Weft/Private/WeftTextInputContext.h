#pragma once

#include "CoreMinimal.h"
#include "GenericPlatform/ITextInputMethodSystem.h"
#include "Layout/Geometry.h"
#include "UObject/WeakObjectPtrTemplates.h"

class SWidget;
class UWeftView;

/**
 * The platform's input method (IMM or TSF, through Slate's ITextInputMethodSystem) at the page that has SWeftViewport's
 * keyboard. weft keeps the text field's value and selection, so the document the input method edits holds only what it
 * composes (and, until the next flush, the text it committed). Its changes reach the page once its edit is over (Flush):
 * the composition as weft_view_composition, the text a composition ends with or that came without one as
 * weft_view_commit_text. Once nothing composes the document empties, and the input method is told so.
 *
 * The composition's range is the one the input method gives (UpdateCompositionRange): IMM gives a composition's new
 * range before its text, TSF after it; an IMM result replaces the whole range. Without a range (Microsoft Pinyin through
 * Unreal's TSF store gives none) the composition is what the input method put in since it began. Unreal's TSF store
 * begins a composition for text an input method inserts without one, which ends only with the next composition's end
 * (or the field's focus), so such text shows as a composition until then.
 */
class FWeftTextInputContext final : public ITextInputMethodContext
{
public:
	explicit FWeftTextInputContext(TWeakPtr<SWidget> InOwner) : Owner(MoveTemp(InOwner)) {}

	/** The page the input method's text goes to, and the geometry of the widget that shows it (where the candidate
	 *  window goes). */
	void SetTarget(UWeftView* InPage, const FGeometry& InGeometry);
	/** Gives the page what the input method did since the last flush; Notifier hears of the document emptying. */
	void Flush(ITextInputMethodChangeNotifier* Notifier);
	/** The page ended the composition (a script set the value, or the field lost the focus), or the input method is
	 *  turned off: what it still does with its composition is dropped, until that ends. */
	void Discard();
	/** An empty document, as the input method is turned on. */
	void Reset();
	/** Whether the page has a composition of the input method's. */
	bool HasComposition() const { return !Sent.IsEmpty(); }

	virtual bool IsComposing() override { return bComposing; }
	virtual bool IsReadOnly() override { return false; }
	virtual uint32 GetTextLength() override { return uint32(Text.Len()); }
	virtual void GetSelectionRange(uint32& OutBeginIndex, uint32& OutLength, ECaretPosition& OutCaretPosition) override;
	virtual void SetSelectionRange(const uint32 InBeginIndex, const uint32 InLength, const ECaretPosition InCaretPosition) override;
	virtual void GetTextInRange(const uint32 InBeginIndex, const uint32 InLength, FString& OutString) override;
	virtual void SetTextInRange(const uint32 InBeginIndex, const uint32 InLength, const FString& InString) override;
	virtual int32 GetCharacterIndexFromPoint(const FVector2D& InPoint) override { return INDEX_NONE; }
	virtual bool GetTextBounds(const uint32 InBeginIndex, const uint32 InLength, FVector2D& OutPosition, FVector2D& OutSize) override;
	virtual void GetScreenBounds(FVector2D& OutPosition, FVector2D& OutSize) override;
	virtual TSharedPtr<FGenericWindow> GetWindow() override;
	virtual void BeginComposition() override;
	virtual void UpdateCompositionRange(const int32 InBeginIndex, const uint32 InLength) override;
	virtual void EndComposition() override;

private:
	/** A rectangle of the page (logical px) in screen space (Slate's absolute coordinates, the desktop's pixels). */
	void ToScreen(const FBox2D& Box, FVector2D& OutPosition, FVector2D& OutSize) const;

	TWeakPtr<SWidget> Owner;
	TWeakObjectPtr<UWeftView> Page;
	FGeometry Geometry;
	FString Text;
	uint32 SelectionBegin = 0;
	uint32 SelectionLength = 0;
	ECaretPosition Caret = ECaretPosition::Ending;
	bool bComposing = false;
	/** The composition's range in Text, once the input method gave one (otherwise Text past Given). */
	bool bRanged = false;
	uint32 CompositionBegin = 0;
	uint32 CompositionLength = 0;
	/** How much of Text's start the page has as committed text; the composition and caret (UTF-16) it has. */
	int32 Given = 0;
	FString Sent;
	int32 SentCursor = 0;
	/** The input method's composition is dropped until it ends (Discard). */
	bool bDiscarding = false;
};
