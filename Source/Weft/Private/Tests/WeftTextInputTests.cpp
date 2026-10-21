// Text fields in Unreal: SWeftViewport's input method context, called as IMM and TSF call it, at the keyboard layer's
// page (a composition, its result, text put in without a composition, a cancelled composition, one the page ended), where
// the candidate window goes, and keys at a text field (a key the input method took, a key that types).
//
//   UnrealEditor-Cmd <project> -ExecCmds="Automation RunTests Weft.TextInput; Quit" -nullrhi -unattended

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS && WITH_WEFT

#include "Input/Events.h"
#include "InputCoreTypes.h"
#include "Layout/Geometry.h"
#include "SWeftViewport.h"
#include "WeftSubsystem.h"
#include "WeftTextInputContext.h"
#include "WeftTypes.h"
#include "WeftView.h"
#include "Windows/WindowsHWrapper.h"

namespace
{
	// A text field with the page's focus; the page logs its keys, compositions and input, and sets the field's value
	// when the engine says so.
	const TCHAR* const Script = TEXT(
		"const root = document.getElementById('root');"
		"const log = s => weft.engine.send('log', s);"
		"const t = root.appendChild(document.createElement('input'));"
		"document.addEventListener('keydown', e => log('keydown:' + e.key + ':' + e.keyCode + (e.isComposing ? ':composing' : '')));"
		"document.addEventListener('keyup', e => log('keyup:' + e.code + (e.isComposing ? ':composing' : '')));"
		"for (const type of ['compositionstart', 'compositionupdate', 'compositionend'])"
		"  t.addEventListener(type, e => log(type + ':' + e.data));"
		"t.addEventListener('input', e => log('input:' + e.inputType + ':' + t.value));"
		"weft.engine.on('value', v => { t.value = v; });"
		"t.focus();");
	const TCHAR* const Css = TEXT(
		"#root{position:relative;width:100%;height:100%;margin:0}"
		"input{position:absolute;left:8px;top:4px;width:100px;height:20px;box-sizing:border-box}");

	// The page's View, kept from the garbage collector, and what it logged.
	struct FFieldPage
	{
		UWeftView* View = nullptr;
		TArray<FString> Log;

		FFieldPage()
		{
			View = UWeftSubsystem::Get()->CreateView();
			View->AddToRoot();
			View->Resize(FVector2D(128, 64), 1);
			View->OnMessage.AddLambda([this](const FString& Name, const FString& Json, uint64)
			{
				if (Name == TEXT("log"))
				{
					Log.Add(Json.TrimQuotes());
				}
			});
			FWeftPage Page;
			Page.Scripts.Emplace(TEXT("page.js"), Script);
			Page.Stylesheet = Css;
			Page.bForgivingCss = true;
			View->Load(Page);
			View->Advance();
			Log.Reset();
		}

		~FFieldPage()
		{
			View->OnMessage.Clear();
			View->RemoveFromRoot();
		}

		// What the page logged since the last call, after an advance that delivers it.
		FString Take()
		{
			View->Advance();
			const FString Out = FString::Join(Log, TEXT(","));
			Log.Reset();
			return Out;
		}
	};

	FKeyEvent Key(const FKey& Key, uint32 VirtualKey)
	{
		return FKeyEvent(Key, FModifierKeysState(), 0, false, 0, VirtualKey);
	}

	// A key the input method took: Windows gives it as VK_PROCESSKEY.
	FKeyEvent Process()
	{
		return Key(EKeys::Invalid, 0xE5);
	}

	bool Running(FAutomationTestBase& Test)
	{
		const UWeftSubsystem* Weft = UWeftSubsystem::Get();
		if (!Weft || !Weft->IsAvailable())
		{
			Test.AddError(TEXT("weft does not run here"));
			return false;
		}
		return true;
	}

	using ECaret = ITextInputMethodContext::ECaretPosition;
} // namespace

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWeftTextInputCompositionTest, "Weft.TextInput.Composition",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FWeftTextInputCompositionTest::RunTest(const FString& Parameters)
{
	if (!Running(*this))
	{
		return false;
	}
	FFieldPage Page;
	UWeftView* View = Page.View;
	const TSharedRef<SWeftViewport> Widget = SNew(SWeftViewport).View(View);
	// The widget 100 px right of and 50 px below the desktop's origin.
	const FGeometry Geometry = FGeometry::MakeRoot(FVector2D(128, 64), FSlateLayoutTransform(FVector2D(100, 50)));
	Widget->OnFocusReceived(Geometry, FFocusEvent(EFocusCause::SetDirectly, 0));
	Page.Take();
	const FWeftTextInput Input = View->GetTextInput();
	TestTrue(TEXT("the focused text field takes text"), Input.bActive && !Input.bComposing && !Input.bPassword);
	TestEqual(TEXT("its border box"), Input.Field.Min, FVector2D(8, 4));
	TestEqual(TEXT("its border box's size"), Input.Field.GetSize(), FVector2D(100, 20));
	TestTrue(TEXT("its caret in it"), Input.Field.IsInside(Input.Caret.Min) && Input.Caret.GetSize().Y > 0);

	FWeftTextInputContext& Context = static_cast<FWeftTextInputContext&>(Widget->GetTextInputMethodContext().Get());
	// IMM: "n" begins a composition (its key comes first; the composition's range comes before its text), "i" goes on,
	// Space gives the result 你 (the whole composition replaced), then the composition's end, twice.
	TestTrue(TEXT("a key the input method took is the page's"), Widget->OnKeyDown(Geometry, Process()).IsEventHandled());
	Context.BeginComposition();
	Context.UpdateCompositionRange(0, 0);
	Context.UpdateCompositionRange(0, 1);
	Context.SetTextInRange(0, 0, TEXT("n"));
	Context.SetSelectionRange(1, 0, ECaret::Ending);
	Widget->OnKeyUp(Geometry, Key(EKeys::N, 'N'));
	TestTrue(TEXT("the page composes"), View->GetTextInput().bComposing);
	FVector2D Position;
	FVector2D Size;
	Context.GetTextBounds(0, 1, Position, Size);
	const FBox2D Caret = View->GetTextInput().Caret;
	TestEqual(TEXT("the candidate window goes below the caret, on the desktop"), Position, Caret.Min + FVector2D(100, 50));
	TestEqual(TEXT("the caret's size"), Size, Caret.GetSize());
	Widget->OnKeyDown(Geometry, Process());
	Context.UpdateCompositionRange(0, 2);
	Context.SetTextInRange(0, 1, TEXT("ni"));
	Context.SetSelectionRange(2, 0, ECaret::Ending);
	Widget->OnKeyUp(Geometry, Key(EKeys::I, 'I'));
	Widget->OnKeyDown(Geometry, Process());
	Context.SetTextInRange(0, 2, TEXT("你"));
	Context.SetSelectionRange(1, 0, ECaret::Ending);
	Context.EndComposition();
	Context.EndComposition();
	Widget->OnKeyUp(Geometry, Key(EKeys::SpaceBar, VK_SPACE));
	TestTrue(TEXT("the document empties after the result"), Context.GetTextLength() == 0);
	TestEqual(TEXT("IMM"), Page.Take(), FString(TEXT(
		"keydown:Process:229,compositionstart:,compositionupdate:n,input:insertCompositionText:n,keyup:KeyN:composing,"
		"keydown:Process:229:composing,compositionupdate:ni,input:insertCompositionText:ni,keyup:KeyI:composing,"
		"keydown:Process:229:composing,compositionupdate:你,input:insertCompositionText:你,compositionend:你,"
		"keyup:Space")));

	// TSF: the edit comes before its key, the composition's range after its text.
	Context.BeginComposition();
	Context.SetTextInRange(0, 0, TEXT("h"));
	Context.SetSelectionRange(1, 0, ECaret::Ending);
	Context.UpdateCompositionRange(0, 1);
	Widget->OnKeyDown(Geometry, Process());
	Context.SetSelectionRange(0, 1, ECaret::Ending);
	Context.SetTextInRange(0, 1, TEXT("哈"));
	Context.SetSelectionRange(1, 0, ECaret::Ending);
	Context.UpdateCompositionRange(0, 1);
	Context.EndComposition();
	Widget->OnKeyDown(Geometry, Process());
	// Text an input method puts in without a composition (Unreal's TSF store begins one for it).
	Context.BeginComposition();
	Context.SetTextInRange(0, 0, TEXT("，"));
	Context.SetSelectionRange(1, 0, ECaret::Ending);
	Widget->OnKeyUp(Geometry, Key(EKeys::Comma, VK_OEM_COMMA));
	TestFalse(TEXT("no composition goes on"), Context.IsComposing());
	TestEqual(TEXT("TSF"), Page.Take(), FString(TEXT(
		"keydown:Process:229,compositionstart:,compositionupdate:h,input:insertCompositionText:你h,"
		"keydown:Process:229:composing,compositionupdate:哈,input:insertCompositionText:你哈,compositionend:哈,"
		"input:insertText:你哈，,keyup:Comma")));

	// IMM cancels a composition (Escape): its range empties before its text.
	Context.BeginComposition();
	Context.UpdateCompositionRange(0, 0);
	Context.UpdateCompositionRange(0, 1);
	Context.SetTextInRange(0, 0, TEXT("x"));
	Context.SetSelectionRange(1, 0, ECaret::Ending);
	Widget->OnKeyUp(Geometry, Key(EKeys::X, 'X'));
	Context.UpdateCompositionRange(0, 0);
	Context.SetSelectionRange(0, 0, ECaret::Beginning);
	Context.SetTextInRange(0, 1, FString());
	Context.EndComposition();
	Widget->OnKeyUp(Geometry, Key(EKeys::Escape, VK_ESCAPE));
	TestEqual(TEXT("a cancelled composition"), Page.Take(), FString(TEXT(
		"compositionstart:,compositionupdate:x,input:insertCompositionText:你哈，x,keyup:KeyX:composing,"
		"compositionupdate:,input:insertCompositionText:你哈，,compositionend:,keyup:Escape")));

	// A key that types into the field is the page's, its text too.
	TestTrue(TEXT("a key that types"), Widget->OnKeyDown(Geometry, Key(EKeys::A, 'A')).IsEventHandled());
	TestTrue(TEXT("its text"), Widget->OnKeyChar(Geometry, FCharacterEvent(TEXT('a'), FModifierKeysState(), 0, false)).IsEventHandled());
	Widget->OnKeyUp(Geometry, Key(EKeys::A, 'A'));
	Page.Take();

	// The page ends a composition (a script sets the value): what the input method still does with it is dropped.
	Context.BeginComposition();
	Context.UpdateCompositionRange(0, 1);
	Context.SetTextInRange(0, 0, TEXT("y"));
	Context.SetSelectionRange(1, 0, ECaret::Ending);
	Widget->OnKeyUp(Geometry, Key(EKeys::Y, 'Y'));
	View->Emit(TEXT("value"), TEXT("\"set\""));
	Page.Take();
	TestFalse(TEXT("the value set ends the page's composition"), View->GetTextInput().bComposing);
	Widget->Tick(Geometry, 0, 0.016f);
	Context.UpdateCompositionRange(0, 2);
	Context.SetTextInRange(0, 1, TEXT("yo"));
	Context.EndComposition();
	Widget->OnKeyUp(Geometry, Key(EKeys::O, 'O'));
	const FString Ended = Page.Take();
	TestFalse(TEXT("nothing of it reaches the page"), Ended.Contains(TEXT("composition")) || Ended.Contains(TEXT("input:")));
	TestTrue(TEXT("and the document empties"), Context.GetTextLength() == 0);
	return true;
}

#endif
