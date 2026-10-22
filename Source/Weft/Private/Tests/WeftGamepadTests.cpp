// Gamepads in Unreal: a View's page sees the states it is given after a gesture, and with spatial navigation the
// D-pad, A and B of a page with the focus are its arrow keys, Enter and Escape; FWeftGamepadInput turns the events
// Slate gives (buttons as they come, the sticks' and triggers' values at the end of the frame's input) into those
// states, a slot each, and consumes what a page used.
//
//   UnrealEditor-Cmd <project> -ExecCmds="Automation RunTests Weft.Gamepad; Quit" -nullrhi -unattended

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS && WITH_WEFT

#include "Input/Events.h"
#include "InputCoreTypes.h"
#include "WeftGamepadInput.h"
#include "WeftSubsystem.h"
#include "WeftTypes.h"
#include "WeftView.h"

namespace
{
	// Three buttons one under the other; the page logs its keys, focus, clicks and gamepad events, and on 'probe' the
	// first gamepad: how many of its buttons are pressed, whether its left trigger is, and its axes.
	const TCHAR* const Script = TEXT(
		"const root = document.getElementById('root');"
		"const log = s => weft.engine.send('log', s);"
		"for (const id of ['a', 'b', 'c']) {"
		"  const n = root.appendChild(document.createElement('button'));"
		"  n.setAttribute('id', id);"
		"  for (const type of ['focus', 'click']) n.addEventListener(type, () => log(type + ':' + id));"
		"}"
		"document.addEventListener('keydown', e => log('keydown:' + e.key));"
		"document.addEventListener('keyup', e => log('keyup:' + e.key));"
		"addEventListener('gamepadconnected', e => log('connected:' + e.gamepad.index + ':' + e.gamepad.id));"
		"addEventListener('gamepaddisconnected', e => log('disconnected:' + e.gamepad.index));"
		"weft.engine.on('probe', () => { const g = navigator.getGamepads()[0];"
		"  log(g ? 'pad:' + g.buttons.filter(b => b.pressed).length + ':' + g.buttons[6].pressed + ':' +"
		"    g.axes.map(a => a.toFixed(2)).join(',') : 'pad:none'); });");
	const TCHAR* const Css = TEXT(
		"#root{position:relative;width:100%;height:100%;margin:0}"
		"button{display:block;width:40px;height:20px;margin:4px;border:0;padding:0}");

	// A View of the page, kept from the garbage collector, and what the page logged.
	struct FGamepadPage
	{
		UWeftView* View = nullptr;
		TArray<FString> Log;

		explicit FGamepadPage(bool bSpatialNavigation)
		{
			View = UWeftSubsystem::Get()->CreateView();
			View->AddToRoot();
			View->Resize(FVector2D(64, 96), 1);
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
			Page.bSpatialNavigation = bSpatialNavigation;
			View->Load(Page);
			View->Advance();
			Log.Reset();
		}

		~FGamepadPage()
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

		FString Probe()
		{
			View->Emit(TEXT("probe"), TEXT("null"));
			return Take();
		}
	};

	FWeftGamepadState Pad(uint32 Pressed = 0, float X = 0.f, float Y = 0.f)
	{
		FWeftGamepadState State;
		State.Id = FWeftGamepadInput::XInputId;
		State.Pressed = Pressed;
		for (int32 I = 0; I < 17; ++I)
		{
			State.Buttons[I] = (Pressed >> I & 1) != 0 ? 1.f : 0.f;
		}
		State.Axes[0] = X;
		State.Axes[1] = Y;
		return State;
	}

	constexpr uint32 ButtonA = 1u << 0;
	constexpr uint32 ButtonB = 1u << 1;
	constexpr uint32 ButtonDpadDown = 1u << 13;

	FKeyEvent Button(const FKey& Key, int32 Device, bool bRepeat = false)
	{
		return FKeyEvent(Key, FModifierKeysState(), FInputDeviceId::CreateFromInternalId(Device), bRepeat, 0, 0);
	}

	FAnalogInputEvent Axis(const FKey& Key, int32 Device, float Value)
	{
		return FAnalogInputEvent(Key, FModifierKeysState(), FInputDeviceId::CreateFromInternalId(Device), false, 0, 0, Value);
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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWeftGamepadViewTest, "Weft.Gamepad.View",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FWeftGamepadViewTest::RunTest(const FString& Parameters)
{
	if (!Running(*this))
	{
		return false;
	}
	const FString Connected = FString(TEXT("connected:0:")) + FWeftGamepadInput::XInputId;
	{
		// Without spatial navigation: the page sees the gamepad after a gesture, and gets no keys.
		FGamepadPage Page(false);
		TestTrue(TEXT("an axis at half is no gesture"), Page.View->Gamepad(Pad(0, 0.5f)) == EWeftInput::None);
		TestEqual(TEXT("no gamepad before a gesture"), Page.Probe(), FString(TEXT("pad:none")));
		TestTrue(TEXT("A, no key"), Page.View->Gamepad(Pad(ButtonA, 0.5f)) == EWeftInput::None);
		TestEqual(TEXT("the gesture"), Page.Probe(), Connected + TEXT(",pad:1:false:0.50,0.00,0.00,0.00"));
		FWeftGamepadState Gone = Pad();
		Gone.bConnected = false;
		Page.View->Gamepad(Gone);
		TestEqual(TEXT("it goes"), Page.Take(), FString(TEXT("disconnected:0")));
	}
	{
		// With spatial navigation, on a page with the focus (a View starts with it): the D-pad, A and B.
		FGamepadPage Page(true);
		TestTrue(TEXT("the D-pad's down moves the focus"), Page.View->Gamepad(Pad(ButtonDpadDown)) == EWeftInput::Default);
		TestEqual(TEXT("ArrowDown"), Page.Take(), Connected + TEXT(",keydown:ArrowDown,focus:a"));
		Page.View->Gamepad(Pad());
		TestEqual(TEXT("its keyup"), Page.Take(), FString(TEXT("keyup:ArrowDown")));
		TestTrue(TEXT("the left stick past half"), Page.View->Gamepad(Pad(0, 0.25f, 0.75f)) == EWeftInput::Default);
		Page.View->Gamepad(Pad());
		TestEqual(TEXT("ArrowDown again"), Page.Take(), FString(TEXT("keydown:ArrowDown,focus:b,keyup:ArrowDown")));
		TestTrue(TEXT("A clicks"), Page.View->Gamepad(Pad(ButtonA)) == EWeftInput::Default);
		TestTrue(TEXT("B blurs"), Page.View->Gamepad(Pad(ButtonB)) == EWeftInput::Default);
		Page.View->Gamepad(Pad());
		TestEqual(TEXT("Enter and Escape"), Page.Take(),
			FString(TEXT("keydown:Enter,click:b,keyup:Enter,keydown:Escape,keyup:Escape")));
		// Without the focus, no keys.
		Page.View->SetFocus(false);
		Page.Take();
		TestTrue(TEXT("no keys without the focus"), Page.View->Gamepad(Pad(ButtonDpadDown)) == EWeftInput::None);
		TestEqual(TEXT("nothing"), Page.Take(), FString());
		Page.View->Gamepad(Pad());
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWeftGamepadInputTest, "Weft.Gamepad.Input",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FWeftGamepadInputTest::RunTest(const FString& Parameters)
{
	if (!Running(*this))
	{
		return false;
	}
	FGamepadPage Page(true);
	FWeftGamepadInput Input([&Page](TArray<UWeftView*>& Out) { Out.Add(Page.View); });
	const int32 Device = 7;
	// A button as it comes: the page used the D-pad's down, so it is consumed, with Slate's repeats and its keyup.
	TestTrue(TEXT("the D-pad's down is the page's"), Input.Key(Button(EKeys::Gamepad_DPad_Down, Device), true));
	TestEqual(TEXT("ArrowDown"), Page.Take(),
		FString(TEXT("connected:0:")) + FWeftGamepadInput::XInputId + TEXT(",keydown:ArrowDown,focus:a"));
	TestTrue(TEXT("Slate's repeat of it"), Input.Key(Button(EKeys::Gamepad_DPad_Down, Device, true), true));
	TestTrue(TEXT("its keyup"), Input.Key(Button(EKeys::Gamepad_DPad_Down, Device), false));
	TestEqual(TEXT("one keydown, one keyup"), Page.Take(), FString(TEXT("keyup:ArrowDown")));
	// A button the page does not use is the game's.
	TestFalse(TEXT("X"), Input.Key(Button(EKeys::Gamepad_FaceButton_Left, Device), true));
	TestFalse(TEXT("X's keyup"), Input.Key(Button(EKeys::Gamepad_FaceButton_Left, Device), false));
	TestFalse(TEXT("a key that is not a gamepad's"), Input.Key(Button(EKeys::Down, Device), true));
	// The left stick (Slate's y up): its values reach the page at the end of the frame's input, in one state.
	TestFalse(TEXT("the stick's x"), Input.Analog(Axis(EKeys::Gamepad_LeftX, Device, 0.25f)));
	TestFalse(TEXT("the stick's y, before the page has it"), Input.Analog(Axis(EKeys::Gamepad_LeftY, Device, -0.75f)));
	TestEqual(TEXT("nothing before the frame's input ends"), Page.Take(), FString());
	Input.Flush();
	TestEqual(TEXT("the stick down"), Page.Take(), FString(TEXT("keydown:ArrowDown,focus:b")));
	TestTrue(TEXT("then the page has the stick"), Input.Analog(Axis(EKeys::Gamepad_LeftX, Device, 0.2f)));
	TestTrue(TEXT("and Slate's key for its direction"), Input.Key(Button(EKeys::Gamepad_LeftStick_Down, Device), true));
	TestFalse(TEXT("not the right stick"), Input.Analog(Axis(EKeys::Gamepad_RightX, Device, 0.9f)));
	Input.Analog(Axis(EKeys::Gamepad_LeftY, Device, 0.f));
	Input.Analog(Axis(EKeys::Gamepad_RightX, Device, 0.f));
	Input.Flush();
	TestEqual(TEXT("the stick back"), Page.Take(), FString(TEXT("keyup:ArrowDown")));
	TestFalse(TEXT("the stick is the game's again"), Input.Analog(Axis(EKeys::Gamepad_LeftX, Device, 0.1f)));
	// A trigger is pressed from 30 of 255.
	TestFalse(TEXT("the left trigger"), Input.Analog(Axis(EKeys::Gamepad_LeftTriggerAxis, Device, 0.2f)));
	Input.Flush();
	TestEqual(TEXT("its value"), Page.Probe(), FString(TEXT("pad:1:true:0.10,0.00,0.00,0.00")));
	// A second gamepad takes the next slot; one that goes is disconnected.
	Input.Key(Button(EKeys::Gamepad_FaceButton_Top, Device + 1), true);
	TestEqual(TEXT("the second gamepad"), Page.Take(), FString(TEXT("connected:1:")) + FWeftGamepadInput::XInputId);
	Input.Disconnect(FInputDeviceId::CreateFromInternalId(Device));
	TestEqual(TEXT("the first goes"), Page.Take(), FString(TEXT("disconnected:0")));
	Input.Disconnect(FInputDeviceId::CreateFromInternalId(Device + 1));
	Page.Take();
	return true;
}

#endif
