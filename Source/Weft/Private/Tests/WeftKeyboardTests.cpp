// The keyboard in Unreal: keys Slate gives (their code from the Windows virtual key, their modifiers) at a page's focused
// element, the text they type, what the page used, and SWeftViewport's keyboard focus: the layer a press or FocusView
// gives it, the pages' focus following Slate's, and the keys a page leaves to the game.
//
//   UnrealEditor-Cmd <project> -ExecCmds="Automation RunTests Weft.Keyboard; Quit" -nullrhi -unattended

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS && WITH_WEFT

#include "Input/Events.h"
#include "InputCoreTypes.h"
#include "Layout/Geometry.h"
#include "SWeftViewport.h"
#include "WeftSubsystem.h"
#include "WeftTypes.h"
#include "WeftView.h"
#include "Windows/WindowsHWrapper.h"
#include "weft/weft.h"

// The module's files share unity translation units: each test file keeps its helpers in a namespace of its own.
namespace WeftKeyboardTests
{
namespace
{
	// Two buttons; the page logs its keyboard and focus events.
	const TCHAR* const Script = TEXT(
		"const root = document.getElementById('root');"
		"const log = s => weft.engine.send('log', s);"
		"for (const id of ['b1', 'b2']) {"
		"  const b = root.appendChild(document.createElement('button'));"
		"  b.className = id;"
		"  for (const type of ['focus', 'blur', 'click']) b.addEventListener(type, () => log(type + ':' + id));"
		"}"
		"document.addEventListener('keydown', e => {"
		"  log('keydown:' + e.code + ':' + e.key + (e.repeat ? ':repeat' : '') + (e.shiftKey ? ':shift' : ''));"
		"  if (e.code === 'KeyW') e.preventDefault();"
		"});"
		"document.addEventListener('keyup', e => log('keyup:' + e.code));"
		"document.addEventListener('keypress', e => log('keypress:' + e.key));"
		"window.addEventListener('focus', () => log('window:focus'));"
		"window.addEventListener('blur', () => log('window:blur'));");
	const TCHAR* const Css = TEXT(
		"#root{position:relative;width:100%;height:100%;margin:0}"
		".b1{position:absolute;left:0;top:0;width:16px;height:16px}"
		".b2{position:absolute;left:16px;top:0;width:16px;height:16px}");

	// A View of the page, kept from the garbage collector, and what the page logged.
	struct FLoggedPage
	{
		UWeftView* View = nullptr;
		TArray<FString> Log;

		explicit FLoggedPage(const TCHAR* MoreCss = TEXT(""))
		{
			View = UWeftSubsystem::Get()->CreateView();
			View->AddToRoot();
			View->Resize(FVector2D(64, 32), 1);
			View->OnMessage.AddLambda([this](const FString& Name, const FString& Json, uint64)
			{
				if (Name == TEXT("log"))
				{
					Log.Add(Json.TrimQuotes());
				}
			});
			FWeftPage Page;
			Page.Scripts.Emplace(TEXT("page.js"), Script);
			Page.Stylesheet = FString(Css) + MoreCss;
			Page.bForgivingCss = true;
			View->Load(Page);
			View->Advance();
			Log.Reset();
		}

		~FLoggedPage()
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

	FKeyEvent Key(const FKey& Key, uint32 VirtualKey, const FModifierKeysState& Modifiers = FModifierKeysState(), bool bRepeat = false)
	{
		return FKeyEvent(Key, Modifiers, 0, bRepeat, 0, VirtualKey);
	}

	FModifierKeysState LeftShift()
	{
		return FModifierKeysState(true, false, false, false, false, false, false, false, false);
	}

	// The code and value the system's layout gives a virtual key with the modifiers.
	FString Typed(uint32 VirtualKey, uint32 Modifiers, FString* OutCode = nullptr)
	{
		char Code[32] = "";
		char Value[32] = "";
		weft_keyboard_code(VirtualKey, Code);
		weft_keyboard_key(Code, size_t(FCStringAnsi::Strlen(Code)), Modifiers, Value);
		if (OutCode)
		{
			*OutCode = UTF8_TO_TCHAR(Code);
		}
		return UTF8_TO_TCHAR(Value);
	}

	FPointerEvent Pointer(FVector2D At, bool bLeftDown)
	{
		TSet<FKey> Pressed;
		if (bLeftDown)
		{
			Pressed.Add(EKeys::LeftMouseButton);
		}
		return FPointerEvent(0, At, At, Pressed, EKeys::LeftMouseButton, 0, FModifierKeysState());
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
} // namespace

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWeftKeyboardViewTest, "Weft.Keyboard.View",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FWeftKeyboardViewTest::RunTest(const FString& Parameters)
{
	if (!Running(*this))
	{
		return false;
	}
	FLoggedPage Page;
	UWeftView* View = Page.View;
	TestTrue(TEXT("Tab moves the focus"), View->Key(Key(EKeys::Tab, VK_TAB), true) == EWeftInput::Default);
	TestTrue(TEXT("its keyup is the game's"), View->Key(Key(EKeys::Tab, VK_TAB), false) == EWeftInput::None);
	TestTrue(TEXT("Enter clicks the focused button"), View->Key(Key(EKeys::Enter, VK_RETURN), true) == EWeftInput::Default);
	View->Key(Key(EKeys::Enter, VK_RETURN), false);
	TestEqual(TEXT("Tab and Enter"), Page.Take(),
		FString(TEXT("keydown:Tab:Tab,focus:b1,keyup:Tab,keydown:Enter:Enter,keypress:Enter,click:b1,keyup:Enter")));

	// Shift's own keydown has its flag, its keyup not; a letter's value is what the layout types with Shift.
	FString B;
	const FString Shifted = Typed('B', WEFT_MODIFIER_SHIFT, &B);
	View->Key(Key(EKeys::LeftShift, VK_LSHIFT, LeftShift()), true);
	TestTrue(TEXT("a letter the page leaves"), View->Key(Key(EKeys::B, 'B', LeftShift()), true) == EWeftInput::None);
	View->Key(Key(EKeys::B, 'B', LeftShift()), false);
	View->Key(Key(EKeys::LeftShift, VK_LSHIFT), false);
	TestEqual(TEXT("Shift and a letter"), Page.Take(),
		FString::Printf(TEXT("keydown:ShiftLeft:Shift:shift,keydown:%s:%s:shift,keyup:%s,keyup:ShiftLeft"), *B, *Shifted, *B));

	// A repeat, a key given by its code and its text, a key the page prevented and its dropped text, a gamepad's button.
	TestTrue(TEXT("a repeat"), View->Key(Key(EKeys::Tab, VK_TAB, FModifierKeysState(), true), true) == EWeftInput::Default);
	View->Key(Key(EKeys::Tab, VK_TAB), false);
	TestTrue(TEXT("a key given by its code"), View->Key(true, TEXT("KeyX"), TEXT("x")) == EWeftInput::None);
	TestTrue(TEXT("its text"), View->Text(TEXT("x")) == EWeftInput::None);
	View->Key(false, TEXT("KeyX"), TEXT("x"));
	TestTrue(TEXT("a prevented key"), View->Key(true, TEXT("KeyW"), TEXT("w")) == EWeftInput::Prevented);
	TestTrue(TEXT("its text is dropped"), View->Text(TEXT("w")) == EWeftInput::Default);
	View->Key(false, TEXT("KeyW"), TEXT("w"));
	TestTrue(TEXT("a gamepad's button is no key"), View->Key(Key(EKeys::Gamepad_FaceButton_Bottom, 0), true) == EWeftInput::None);
	TestEqual(TEXT("repeat, text, prevention"), Page.Take(),
		FString(TEXT("keydown:Tab:Tab:repeat,blur:b1,focus:b2,keyup:Tab,keydown:KeyX:x,keypress:x,keyup:KeyX,keydown:KeyW:w,keyup:KeyW")));

	View->SetFocus(false);
	View->SetFocus(true);
	TestEqual(TEXT("the window's focus"), Page.Take(), FString(TEXT("blur:b2,window:blur,window:focus,focus:b2")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWeftKeyboardViewportTest, "Weft.Keyboard.Viewport",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FWeftKeyboardViewportTest::RunTest(const FString& Parameters)
{
	if (!Running(*this))
	{
		return false;
	}
	FLoggedPage First;
	// The second page's first button where the first page has nothing.
	FLoggedPage Second(TEXT(".b1{left:40px;top:16px}"));
	const TSharedRef<SWeftViewport> Widget = SNew(SWeftViewport).View(First.View);
	Widget->AddView(Second.View, 1);
	const FGeometry Geometry = FGeometry::MakeRoot(FVector2D(64, 32), FSlateLayoutTransform());
	TestEqual(TEXT("without Slate's focus no page has it"), First.Take() + TEXT("|") + Second.Take(),
		FString(TEXT("window:blur|window:blur")));
	TestTrue(TEXT("the topmost interactive layer has the keyboard first"), Widget->GetKeyboardView() == Second.View);

	// Slate's focus comes: the keyboard layer's page gets the keys; those it leaves go on to the game.
	Widget->OnFocusReceived(Geometry, FFocusEvent(EFocusCause::SetDirectly, 0));
	TestTrue(TEXT("Tab is the page's"), Widget->OnKeyDown(Geometry, Key(EKeys::Tab, VK_TAB)).IsEventHandled());
	TestFalse(TEXT("its keyup goes on"), Widget->OnKeyUp(Geometry, Key(EKeys::Tab, VK_TAB)).IsEventHandled());
	FString Q;
	const FString TypedQ = Typed('Q', 0, &Q);
	TestFalse(TEXT("a letter the page leaves goes on"), Widget->OnKeyDown(Geometry, Key(EKeys::Q, 'Q')).IsEventHandled());
	TestFalse(TEXT("its text too"), Widget->OnKeyChar(Geometry, FCharacterEvent(TEXT('q'), FModifierKeysState(), 0, false)).IsEventHandled());
	Widget->OnKeyUp(Geometry, Key(EKeys::Q, 'Q'));
	// A character beyond the Basic Multilingual Plane comes as two surrogates.
	Widget->OnKeyChar(Geometry, FCharacterEvent(TCHAR(0xD83D), FModifierKeysState(), 0, false));
	Widget->OnKeyChar(Geometry, FCharacterEvent(TCHAR(0xDE00), FModifierKeysState(), 0, false));
	const FString Smile = FString::ConstructFromPtrSize(L"\xD83D\xDE00", 2);
	TestEqual(TEXT("the keyboard layer's page gets the keys"), Second.Take(),
		FString::Printf(TEXT("window:focus,keydown:Tab:Tab,focus:b1,keyup:Tab,keydown:%s:%s,keypress:q,keyup:%s,keypress:%s"), *Q,
			*TypedQ, *Q, *Smile));
	TestEqual(TEXT("the other none"), First.Take(), FString());

	// The second layer goes under the first, which is then the topmost and has the keyboard.
	Widget->RemoveView(Second.View);
	TestEqual(TEXT("a layer taken away keeps its focus"), Second.Take(), FString());
	Widget->AddView(Second.View, -1);
	TestTrue(TEXT("the topmost layer"), Widget->GetKeyboardView() == First.View);
	TestEqual(TEXT("the topmost page gets the focus"), First.Take(), FString(TEXT("window:focus")));
	TestEqual(TEXT("the page under it loses it"), Second.Take(), FString(TEXT("blur:b1,window:blur")));

	// A press on the second's page, where the first has nothing, gives the second the keyboard before its mousedown:
	// the window's focus, then its focused button's, then the click. Slate applies no replies of handlers called here
	// (the capture), so the page gets its release directly.
	const FVector2D At(48, 24);
	Widget->OnMouseMove(Geometry, Pointer(At, false));
	TestTrue(TEXT("a press is the page's"), Widget->OnMouseButtonDown(Geometry, Pointer(At, true)).IsEventHandled());
	Second.View->Pointer(EWeftPointer::Up, At, 0);
	TestTrue(TEXT("its layer has the keyboard"), Widget->GetKeyboardView() == Second.View);
	TestEqual(TEXT("the pressed page has the focus first"), Second.Take(), FString(TEXT("window:focus,focus:b1,click:b1")));
	TestEqual(TEXT("the page that had it"), First.Take(), FString(TEXT("window:blur")));

	// FocusView, and Slate's focus going elsewhere.
	Widget->FocusView(First.View);
	TestTrue(TEXT("FocusView"), Widget->GetKeyboardView() == First.View);
	TestEqual(TEXT("the view given the keyboard"), First.Take(), FString(TEXT("window:focus")));
	TestEqual(TEXT("the view that had it"), Second.Take(), FString(TEXT("blur:b1,window:blur")));
	Widget->OnFocusLost(FFocusEvent(EFocusCause::Mouse, 0));
	TestEqual(TEXT("Slate's focus went elsewhere"), First.Take(), FString(TEXT("window:blur")));
	Widget->AddView(First.View, 0, /*bInteractive*/ false);
	TestTrue(TEXT("a layer that is not interactive has no keyboard"), Widget->GetKeyboardView() == Second.View);
	return true;
}

} // namespace WeftKeyboardTests

#endif
