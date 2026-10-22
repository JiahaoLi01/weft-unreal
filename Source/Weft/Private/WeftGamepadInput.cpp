#include "WeftGamepadInput.h"

#include "GenericPlatform/GenericPlatformInputDeviceMapper.h"
#include "Input/Events.h"
#include "InputCoreTypes.h"

namespace
{
	// How far the left stick goes before it is an arrow key (weft's rule), and the bits past the buttons' of the
	// directions it is then (up, down, left, right), each held as a key of its own.
	constexpr float StickArrow = 0.5f;
	constexpr int32 StickBits = 17;
	constexpr uint32 StickMask = 0xFu << StickBits;
	// Chrome's press point of an XInput trigger (XINPUT_GAMEPAD_TRIGGER_THRESHOLD of 255).
	constexpr float TriggerPressed = 30.f / 255.f;

	// The standard button of a key Slate gives for a gamepad's button; INDEX_NONE for the others (the triggers come by
	// their values).
	int32 ButtonOf(const FKey& Key)
	{
		static const TMap<FKey, int32> Buttons = {
			{EKeys::Gamepad_FaceButton_Bottom, 0},
			{EKeys::Gamepad_FaceButton_Right, 1},
			{EKeys::Gamepad_FaceButton_Left, 2},
			{EKeys::Gamepad_FaceButton_Top, 3},
			{EKeys::Gamepad_LeftShoulder, 4},
			{EKeys::Gamepad_RightShoulder, 5},
			{EKeys::Gamepad_Special_Left, 8},
			{EKeys::Gamepad_Special_Right, 9},
			{EKeys::Gamepad_LeftThumbstick, 10},
			{EKeys::Gamepad_RightThumbstick, 11},
			{EKeys::Gamepad_DPad_Up, 12},
			{EKeys::Gamepad_DPad_Down, 13},
			{EKeys::Gamepad_DPad_Left, 14},
			{EKeys::Gamepad_DPad_Right, 15},
		};
		const int32* Button = Buttons.Find(Key);
		return Button ? *Button : INDEX_NONE;
	}

	// The keys Slate makes of the left stick's directions.
	bool IsLeftStickKey(const FKey& Key)
	{
		return Key == EKeys::Gamepad_LeftStick_Up || Key == EKeys::Gamepad_LeftStick_Down ||
			Key == EKeys::Gamepad_LeftStick_Left || Key == EKeys::Gamepad_LeftStick_Right;
	}

	// The inputs a state holds: its buttons pressed, and the left stick's direction past half (by its larger axis).
	uint32 HeldOf(const FWeftGamepadState& State)
	{
		const float X = State.Axes[0];
		const float Y = State.Axes[1];
		if (FMath::Max(FMath::Abs(X), FMath::Abs(Y)) <= StickArrow)
		{
			return State.Pressed;
		}
		const int32 Direction = FMath::Abs(X) > FMath::Abs(Y) ? (X < 0 ? 2 : 3) : (Y < 0 ? 0 : 1);
		return State.Pressed | 1u << (StickBits + Direction);
	}
} // namespace

const TCHAR* const FWeftGamepadInput::XInputId = TEXT("Xbox 360 Controller (XInput STANDARD GAMEPAD)");

FWeftGamepadInput::FWeftGamepadInput(TFunction<void(TArray<UWeftView*>&)> InViews)
	: Views(MoveTemp(InViews))
{
	Connection = IPlatformInputDeviceMapper::Get().GetOnInputDeviceConnectionChange().AddRaw(
		this, &FWeftGamepadInput::OnConnectionChange);
}

FWeftGamepadInput::~FWeftGamepadInput()
{
	IPlatformInputDeviceMapper::Get().GetOnInputDeviceConnectionChange().Remove(Connection);
}

void FWeftGamepadInput::Tick(const float DeltaTime, FSlateApplication& SlateApp, TSharedRef<ICursor> Cursor)
{
	Flush();
}

bool FWeftGamepadInput::HandleKeyDownEvent(FSlateApplication& SlateApp, const FKeyEvent& InKeyEvent)
{
	return Key(InKeyEvent, true);
}

bool FWeftGamepadInput::HandleKeyUpEvent(FSlateApplication& SlateApp, const FKeyEvent& InKeyEvent)
{
	return Key(InKeyEvent, false);
}

bool FWeftGamepadInput::HandleAnalogInputEvent(FSlateApplication& SlateApp, const FAnalogInputEvent& InAnalogInputEvent)
{
	return Analog(InAnalogInputEvent);
}

int32 FWeftGamepadInput::SlotOf(FInputDeviceId Device, bool bCreate)
{
	int32 Free = INDEX_NONE;
	for (int32 I = 0; I < int32(UE_ARRAY_COUNT(Slots)); ++I)
	{
		if (Slots[I].bInUse && Slots[I].Device == Device)
		{
			return I;
		}
		Free = Free == INDEX_NONE && !Slots[I].bInUse ? I : Free;
	}
	if (!bCreate || Free == INDEX_NONE)
	{
		return INDEX_NONE;
	}
	FSlot& Slot = Slots[Free];
	Slot = FSlot();
	Slot.bInUse = true;
	Slot.Device = Device;
	Slot.State.Index = Free;
	Slot.State.Id = XInputId;
	return Free;
}

bool FWeftGamepadInput::Key(const FKeyEvent& Event, bool bDown)
{
	const FKey Key = Event.GetKey();
	if (!Key.IsGamepadKey())
	{
		return false;
	}
	const int32 Button = ButtonOf(Key);
	const int32 At = SlotOf(Event.GetInputDeviceId(), Button != INDEX_NONE && bDown);
	if (At == INDEX_NONE)
	{
		return false;
	}
	FSlot& Slot = Slots[At];
	if (IsLeftStickKey(Key))
	{
		return (Slot.Has & StickMask) != 0;
	}
	if (Button == INDEX_NONE)
	{
		return false;
	}
	const uint32 Bit = 1u << Button;
	const bool bHad = (Slot.Has & Bit) != 0;
	// Slate repeats a held button; weft repeats a held direction itself.
	if (Event.IsRepeat())
	{
		return bHad;
	}
	Slot.State.Buttons[Button] = bDown ? 1.f : 0.f;
	Slot.State.Pressed = bDown ? Slot.State.Pressed | Bit : Slot.State.Pressed & ~Bit;
	Send(Slot);
	return bDown ? (Slot.Has & Bit) != 0 : bHad;
}

bool FWeftGamepadInput::Analog(const FAnalogInputEvent& Event)
{
	const FKey Key = Event.GetKey();
	// The left and right sticks' axes (y down, as the Gamepad API has it), and the triggers' buttons.
	int32 Axis = INDEX_NONE;
	int32 Trigger = INDEX_NONE;
	float Sign = 1.f;
	if (Key == EKeys::Gamepad_LeftX)
	{
		Axis = 0;
	}
	else if (Key == EKeys::Gamepad_LeftY)
	{
		Axis = 1;
		Sign = -1.f;
	}
	else if (Key == EKeys::Gamepad_RightX)
	{
		Axis = 2;
	}
	else if (Key == EKeys::Gamepad_RightY)
	{
		Axis = 3;
		Sign = -1.f;
	}
	else if (Key == EKeys::Gamepad_LeftTriggerAxis)
	{
		Trigger = 6;
	}
	else if (Key == EKeys::Gamepad_RightTriggerAxis)
	{
		Trigger = 7;
	}
	else
	{
		return false;
	}
	const float Value = Event.GetAnalogValue();
	const int32 At = SlotOf(Event.GetInputDeviceId(), Value != 0.f);
	if (At == INDEX_NONE)
	{
		return false;
	}
	FSlot& Slot = Slots[At];
	if (Axis != INDEX_NONE)
	{
		const float To = FMath::Clamp(Sign * Value, -1.f, 1.f);
		Slot.bDirty |= Slot.State.Axes[Axis] != To;
		Slot.State.Axes[Axis] = To;
		return Axis < 2 && (Slot.Has & StickMask) != 0;
	}
	const float To = FMath::Clamp(Value, 0.f, 1.f);
	Slot.bDirty |= Slot.State.Buttons[Trigger] != To;
	Slot.State.Buttons[Trigger] = To;
	const uint32 Bit = 1u << Trigger;
	Slot.State.Pressed = To >= TriggerPressed ? Slot.State.Pressed | Bit : Slot.State.Pressed & ~Bit;
	return false;
}

void FWeftGamepadInput::Flush()
{
	for (FSlot& Slot : Slots)
	{
		if (Slot.bInUse && Slot.bDirty)
		{
			Send(Slot);
		}
	}
}

void FWeftGamepadInput::Disconnect(FInputDeviceId Device)
{
	const int32 At = SlotOf(Device, false);
	if (At == INDEX_NONE)
	{
		return;
	}
	FSlot& Slot = Slots[At];
	FWeftGamepadState Gone;
	Gone.Index = At;
	Gone.bConnected = false;
	Gone.Id = Slot.State.Id;
	Slot.State = Gone;
	Send(Slot);
	Slot = FSlot();
}

bool FWeftGamepadInput::Send(FSlot& Slot)
{
	Slot.bDirty = false;
	const uint32 Now = Slot.State.bConnected ? HeldOf(Slot.State) : 0;
	const uint32 WentDown = Now & ~Slot.Held;
	const uint32 CameUp = Slot.Held & ~Now;
	Slot.Held = Now;
	TArray<UWeftView*> Pages;
	Views(Pages);
	bool bUsed = false;
	for (UWeftView* Page : Pages)
	{
		bUsed |= Page->Gamepad(Slot.State) != EWeftInput::None;
	}
	Slot.Has &= ~CameUp;
	if (bUsed)
	{
		Slot.Has |= WentDown;
	}
	return bUsed;
}

void FWeftGamepadInput::OnConnectionChange(EInputDeviceConnectionState NewState, FPlatformUserId User, FInputDeviceId Device)
{
	if (NewState == EInputDeviceConnectionState::Disconnected)
	{
		Disconnect(Device);
	}
}
