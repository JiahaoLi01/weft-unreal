#pragma once

#include "CoreMinimal.h"
#include "Framework/Application/IInputProcessor.h"
#include "WeftView.h"

/**
 * The gamepads Slate reads, for every View's page (UWeftView::Gamepad), as Slate's first input pre-processor: up to four
 * in the Gamepad API's standard layout, a slot each from its first input (a page shows none before a gesture anyway),
 * as Chrome's XInput gamepads ("Xbox 360 Controller (XInput STANDARD GAMEPAD)"; the triggers pressed from 30 of 255,
 * the sticks' values as the platform gives them, y down). A button's change goes to the pages as it comes; the sticks'
 * and triggers' values go at the end of the frame's input, in one state (a stick's x and y come apart).
 *
 * On a page with spatial navigation that has the keyboard focus weft makes the D-pad and the left stick, A and B the
 * arrow keys, Enter and Escape. What a page used is not the game's, as a key a page used: a button whose press it used
 * is consumed until it comes up (Slate's repeats of it too), and the left stick (its values and Slate's keys for its
 * directions) while the page has a direction of it, from the frame after (the stick's values reach the pages at the end
 * of the frame's input).
 */
class FWeftGamepadInput : public IInputProcessor
{
public:
	/** Views gives the Views that get the gamepads (the subsystem's). */
	explicit FWeftGamepadInput(TFunction<void(TArray<UWeftView*>&)> InViews);
	virtual ~FWeftGamepadInput() override;

	virtual void Tick(const float DeltaTime, FSlateApplication& SlateApp, TSharedRef<ICursor> Cursor) override;
	virtual bool HandleKeyDownEvent(FSlateApplication& SlateApp, const FKeyEvent& InKeyEvent) override;
	virtual bool HandleKeyUpEvent(FSlateApplication& SlateApp, const FKeyEvent& InKeyEvent) override;
	virtual bool HandleAnalogInputEvent(FSlateApplication& SlateApp, const FAnalogInputEvent& InAnalogInputEvent) override;
	virtual const TCHAR* GetDebugName() const override { return TEXT("Weft gamepads"); }

	/** A gamepad button going down or up (Slate's key event): whether a page has it, which consumes it. */
	bool Key(const FKeyEvent& Event, bool bDown);
	/** A stick's or a trigger's value: whether a page has it (the left stick's), which consumes it. */
	bool Analog(const FAnalogInputEvent& Event);
	/** The end of the frame's input: the values that changed go to the pages. */
	void Flush();
	/** The gamepad went (the platform's input device mapper says so): the pages' gamepaddisconnected. */
	void Disconnect(FInputDeviceId Device);

	static const TCHAR* const XInputId;

private:
	struct FSlot
	{
		bool bInUse = false;
		FInputDeviceId Device;
		FWeftGamepadState State;
		/** Whether a value changed since the pages got the state. */
		bool bDirty = false;
		/** The inputs held when the pages last got the state (the buttons pressed, by their bits, and the left stick's
		 *  direction past half, by the bits past them), and those a page has: it used their press, until they come up. */
		uint32 Held = 0;
		uint32 Has = 0;
	};

	/** The slot of a device, or a new one for it (bCreate) when one is free; INDEX_NONE for none. */
	int32 SlotOf(FInputDeviceId Device, bool bCreate);
	/** Gives every View the slot's state; whether a page used it. */
	bool Send(FSlot& Slot);
	void OnConnectionChange(EInputDeviceConnectionState NewState, FPlatformUserId User, FInputDeviceId Device);

	TFunction<void(TArray<UWeftView*>&)> Views;
	FSlot Slots[4];
	FDelegateHandle Connection;
};
