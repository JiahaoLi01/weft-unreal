#include "WeftView.h"

#include "HAL/PlatformTime.h"
#include "Input/Events.h"
#include "Misc/FileHelper.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"
#include "Render/WeftFramePacket.h"
#include "WeftModule.h"
#include "WeftValue.h"

#if PLATFORM_WINDOWS
#include "Windows/WindowsHWrapper.h"
#endif

#if WITH_WEFT
namespace
{
	weft_view_handle Handle(uint64 RuntimeId, uint32 Slot, uint32 Generation)
	{
		return {RuntimeId, Slot, Generation};
	}

	FString FromUtf8(const char* Text, size_t Size)
	{
		const FUTF8ToTCHAR Converted(Text, int32(Size));
		return FString(Converted.Length(), Converted.Get());
	}

	// The virtual key Windows gives a key the input method took (VK_PROCESSKEY).
	constexpr uint32 ProcessKey = 0xE5;

	bool Succeeded(weft_result Result, const TCHAR* What)
	{
		if (Result != WEFT_OK)
		{
			UE_LOG(LogWeft, Warning, TEXT("Weft: %s: %s"), What, UTF8_TO_TCHAR(weft_result_string(Result)));
			return false;
		}
		return true;
	}

	// The modifiers of a key Slate gives: its event's, with the Windows keys and Num Lock, which Windows' Slate leaves
	// out. A modifier key's own keydown has its flag; its keyup not, unless the other of its pair is down.
	uint32 ModifiersOf(const FKeyEvent& Event, const char* Code, bool bDown)
	{
		const FModifierKeysState& State = Event.GetModifierKeys();
		bool bLeftMeta = State.IsLeftCommandDown();
		bool bRightMeta = State.IsRightCommandDown();
		uint32 Modifiers = (State.IsShiftDown() ? WEFT_MODIFIER_SHIFT : 0u) |
			(State.IsControlDown() ? WEFT_MODIFIER_CONTROL : 0u) | (State.IsAltDown() ? WEFT_MODIFIER_ALT : 0u) |
			(State.AreCapsLocked() ? WEFT_MODIFIER_CAPS_LOCK : 0u);
#if PLATFORM_WINDOWS
		bLeftMeta = bLeftMeta || (::GetKeyState(VK_LWIN) & 0x8000) != 0;
		bRightMeta = bRightMeta || (::GetKeyState(VK_RWIN) & 0x8000) != 0;
		Modifiers |= (::GetKeyState(VK_NUMLOCK) & 1) != 0 ? WEFT_MODIFIER_NUM_LOCK : 0u;
#endif
		Modifiers |= bLeftMeta || bRightMeta ? WEFT_MODIFIER_META : 0u;
		struct FPair
		{
			const char* Left;
			const char* Right;
			uint32 Flag;
			bool bLeft;
			bool bRight;
		};
		const FPair Pairs[] = {
			{"ShiftLeft", "ShiftRight", WEFT_MODIFIER_SHIFT, State.IsLeftShiftDown(), State.IsRightShiftDown()},
			{"ControlLeft", "ControlRight", WEFT_MODIFIER_CONTROL, State.IsLeftControlDown(), State.IsRightControlDown()},
			{"AltLeft", "AltRight", WEFT_MODIFIER_ALT, State.IsLeftAltDown(), State.IsRightAltDown()},
			{"MetaLeft", "MetaRight", WEFT_MODIFIER_META, bLeftMeta, bRightMeta},
		};
		for (const FPair& Pair : Pairs)
		{
			const bool bLeft = FCStringAnsi::Strcmp(Code, Pair.Left) == 0;
			if (bLeft || FCStringAnsi::Strcmp(Code, Pair.Right) == 0)
			{
				const bool bOther = bLeft ? Pair.bRight : Pair.bLeft;
				Modifiers = bDown || bOther ? Modifiers | Pair.Flag : Modifiers & ~Pair.Flag;
			}
		}
		return Modifiers;
	}

	// A file a page names (a url() image), by the path weft resolved against the page's base: read as the engine reads
	// files, from a pak in a packaged game. Unwritten when there is none (weft diagnoses it).
	weft_result ReadResource(void*, const char* Path, size_t PathSize, weft_resource_writer* File)
	{
		TArray<uint8> Bytes;
		if (!FFileHelper::LoadFileToArray(Bytes, *FromUtf8(Path, PathSize), FILEREAD_Silent))
		{
			return WEFT_OK;
		}
		return weft_resource_writer_set(File, Bytes.GetData(), size_t(Bytes.Num()));
	}
} // namespace
#endif

bool UWeftView::Open(weft_runtime* InRuntime)
{
#if WITH_WEFT
	const weft_view_desc Desc{sizeof(Desc), 0, float(Size.X), float(Size.Y), Scale, 0};
	weft_view_handle View{};
	if (!Succeeded(weft_view_create(InRuntime, &Desc, &View), TEXT("create a view")))
	{
		return false;
	}
	Runtime = InRuntime;
	RuntimeId = View.runtime_id;
	Slot = View.slot;
	Generation = View.generation;
	StartSeconds = FPlatformTime::Seconds();
	Succeeded(weft_view_set_resource_reader(Runtime, View, &ReadResource, nullptr), TEXT("set the resource reader"));
	return true;
#else
	return false;
#endif
}

void UWeftView::Close()
{
#if WITH_WEFT
	if (Runtime)
	{
		weft_view_destroy(Runtime, Handle(RuntimeId, Slot, Generation));
		Runtime = nullptr;
	}
#endif
}

void UWeftView::BeginDestroy()
{
	Close();
	Super::BeginDestroy();
}

bool UWeftView::LoadDirectory(const FString& Directory)
{
	FWeftPage Page;
	FString Error;
	if (!FWeftPage::FromDirectory(Directory, Page, &Error))
	{
		UE_LOG(LogWeft, Error, TEXT("Weft: %s"), *Error);
		return false;
	}
	return Load(Page);
}

bool UWeftView::Load(const FWeftPage& Page)
{
#if WITH_WEFT
	if (!Runtime)
	{
		return false;
	}
	// The page's text as UTF-8, alive for the call.
	TArray<FTCHARToUTF8> Names;
	TArray<FTCHARToUTF8> Sources;
	for (const TPair<FString, FString>& Script : Page.Scripts)
	{
		Names.Emplace(*Script.Key);
		Sources.Emplace(*Script.Value);
	}
	TArray<weft_script> Scripts;
	for (int32 I = 0; I < Names.Num(); ++I)
	{
		Scripts.Add({Names[I].Get(), size_t(Names[I].Length()), Sources[I].Get(), size_t(Sources[I].Length())});
	}
	const FTCHARToUTF8 Stylesheet(*Page.Stylesheet);
	const FTCHARToUTF8 Keyframes(*Page.Keyframes);
	const FTCHARToUTF8 Root(*Page.Root);
	const FTCHARToUTF8 Base(*Page.Base);
	weft_page_desc Desc{sizeof(Desc)};
	Desc.flags = (Page.bCrpgBridge ? WEFT_PAGE_CRPG_BRIDGE : 0u) | (Page.bForgivingCss ? WEFT_PAGE_FORGIVING_CSS : 0u) |
		(Page.bWebPlatform ? WEFT_PAGE_WEB_PLATFORM : 0u) | (Page.bSpatialNavigation ? WEFT_PAGE_SPATIAL_NAVIGATION : 0u);
	Desc.scripts = Scripts.GetData();
	Desc.script_count = uint32(Scripts.Num());
	Desc.stylesheet = Stylesheet.Get();
	Desc.stylesheet_size = size_t(Stylesheet.Length());
	Desc.keyframes = Keyframes.Get();
	Desc.keyframes_size = size_t(Keyframes.Length());
	Desc.root = Page.Root.IsEmpty() ? nullptr : Root.Get();
	Desc.root_size = Page.Root.IsEmpty() ? 0 : size_t(Root.Length());
	Desc.base = Base.Get();
	Desc.base_size = size_t(Base.Length());
	const bool bLoaded = Succeeded(weft_view_load(Runtime, Handle(RuntimeId, Slot, Generation), &Desc), TEXT("load a page"));
	bSpatialNavigation = bLoaded && Page.bSpatialNavigation;
	// The page's exceptions, if any.
	Advance();
	return bLoaded;
#else
	return false;
#endif
}

bool UWeftView::Emit(const FString& Name, const FString& Json, bool bLatest)
{
#if WITH_WEFT
	if (!Runtime)
	{
		return false;
	}
	const FTCHARToUTF8 N(*Name);
	const FTCHARToUTF8 J(*Json);
	return Succeeded(weft_view_emit(Runtime, Handle(RuntimeId, Slot, Generation), N.Get(), N.Length(), WEFT_VALUE_JSON,
						 J.Get(), J.Length(), bLatest ? WEFT_EMIT_LATEST : 0),
		TEXT("emit an event"));
#else
	return false;
#endif
}

bool UWeftView::SetModel(const FString& Model, const FString& FieldsJson)
{
#if WITH_WEFT
	if (!Runtime)
	{
		return false;
	}
	const FTCHARToUTF8 M(*Model);
	const FTCHARToUTF8 J(*FieldsJson);
	return Succeeded(weft_view_set_model(Runtime, Handle(RuntimeId, Slot, Generation), M.Get(), M.Length(), WEFT_VALUE_JSON,
						 J.Get(), J.Length()),
		TEXT("set a view model"));
#else
	return false;
#endif
}

void UWeftView::SetAnchors(const TArray<FWeftAnchor>& InAnchors)
{
	Anchors = InAnchors;
}

bool UWeftView::CompleteCall(uint64 CallId, bool bSucceeded, const FString& Json)
{
#if WITH_WEFT
	if (!Runtime)
	{
		return false;
	}
	const FTCHARToUTF8 J(*Json);
	return Succeeded(weft_view_complete_call(Runtime, Handle(RuntimeId, Slot, Generation), CallId, bSucceeded ? 1 : 0,
						 WEFT_VALUE_JSON, J.Get(), J.Length()),
		TEXT("complete a call"));
#else
	return false;
#endif
}

void UWeftView::Resize(FVector2D LogicalSize, float DeviceScale)
{
	LogicalSize = FVector2D(FMath::Max(LogicalSize.X, 1.0), FMath::Max(LogicalSize.Y, 1.0));
	DeviceScale = FMath::Max(DeviceScale, 0.01f);
	if (LogicalSize == Size && DeviceScale == Scale)
	{
		return;
	}
	Size = LogicalSize;
	Scale = DeviceScale;
#if WITH_WEFT
	if (Runtime)
	{
		Succeeded(weft_view_resize(Runtime, Handle(RuntimeId, Slot, Generation), float(Size.X), float(Size.Y), Scale),
			TEXT("resize a view"));
	}
#endif
}

void UWeftView::SetContentScale(float InContentScale)
{
	ContentScale = FMath::Clamp(InContentScale, 0.1f, 10.f);
}

bool UWeftView::SetStorageItem(const FString& Key, const FString& Value, bool bRemove)
{
#if WITH_WEFT
	if (!Runtime)
	{
		return false;
	}
	const FTCHARToUTF8 K(*Key);
	const FTCHARToUTF8 V(*Value);
	return Succeeded(weft_view_set_storage_item(Runtime, Handle(RuntimeId, Slot, Generation), K.Get(), size_t(K.Length()),
						 bRemove ? nullptr : V.Get(), bRemove ? 0 : size_t(V.Length())),
		TEXT("set a storage item"));
#else
	return false;
#endif
}

bool UWeftView::ClearStorage()
{
#if WITH_WEFT
	return Runtime && Succeeded(weft_view_clear_storage(Runtime, Handle(RuntimeId, Slot, Generation)), TEXT("clear the storage"));
#else
	return false;
#endif
}

bool UWeftView::GetStorage(uint64& Revision, TMap<FString, FString>& OutItems) const
{
#if WITH_WEFT
	if (!Runtime)
	{
		return false;
	}
	TMap<FString, FString> Items;
	uint64 Current = 0;
	const weft_result Result = weft_view_get_storage(
		Runtime, Handle(RuntimeId, Slot, Generation), Revision,
		[](void* User, const char* Key, size_t KeySize, const char* Value, size_t ValueSize)
		{
			static_cast<TMap<FString, FString>*>(User)->Add(FromUtf8(Key, KeySize), FromUtf8(Value, ValueSize));
		},
		&Items, &Current);
	if (Result != WEFT_OK || (Revision && Current == Revision))
	{
		return false;
	}
	Revision = Current;
	OutItems = MoveTemp(Items);
	return true;
#else
	return false;
#endif
}

double UWeftView::GetPageTime() const
{
	return FPlatformTime::Seconds() - StartSeconds;
}

void UWeftView::Advance()
{
#if WITH_WEFT
	if (!Runtime)
	{
		return;
	}
	TRACE_CPUPROFILER_EVENT_SCOPE(Weft_Advance);
	const weft_view_handle View = Handle(RuntimeId, Slot, Generation);
	Succeeded(weft_view_advance(Runtime, View, GetPageTime()), TEXT("advance a view"));
	// The page's messages: handed on after the drain, so that handlers may call into the View.
	struct FMessage
	{
		FString Name;
		FString Json;
		uint64 CallId;
	};
	TArray<FMessage> Messages;
	weft_view_drain_messages(
		Runtime, View,
		[](void* User, weft_view_handle, const weft_message* Message)
		{
			static_cast<TArray<FMessage>*>(User)->Add({FromUtf8(Message->name, Message->name_size),
				WeftValue::MessagePackToJson(Message->payload, Message->payload_size), Message->call_id});
		},
		&Messages);
	// The page's diagnostics in the log, at the level of their kind: the console's lines as written, the rest as
	// warnings and errors.
	TArray<TPair<EWeftDiagnostic, FString>> Diagnostics;
	uint32 Dropped = 0;
	weft_view_drain_diagnostics(
		Runtime, View,
		[](void* User, weft_view_handle, const weft_diagnostic* Diagnostic)
		{
			static_cast<TArray<TPair<EWeftDiagnostic, FString>>*>(User)->Emplace(EWeftDiagnostic(Diagnostic->kind),
				FromUtf8(Diagnostic->message, Diagnostic->message_size));
		},
		&Diagnostics, &Dropped);
	for (const TPair<EWeftDiagnostic, FString>& Diagnostic : Diagnostics)
	{
		switch (Diagnostic.Key)
		{
		case EWeftDiagnostic::Console:
			UE_LOG(LogWeft, Log, TEXT("Weft page: %s"), *Diagnostic.Value);
			break;
		case EWeftDiagnostic::ConsoleWarning:
		case EWeftDiagnostic::Scene:
			UE_LOG(LogWeft, Warning, TEXT("Weft page: %s"), *Diagnostic.Value);
			break;
		default:
			UE_LOG(LogWeft, Error, TEXT("Weft page: %s"), *Diagnostic.Value);
			break;
		}
		OnDiagnostic.Broadcast(Diagnostic.Key, Diagnostic.Value);
	}
	if (Dropped)
	{
		UE_LOG(LogWeft, Warning, TEXT("Weft page: %u diagnostics dropped past the view's bound"), Dropped);
	}
	for (const FMessage& Message : Messages)
	{
		OnMessage.Broadcast(Message.Name, Message.Json, Message.CallId);
	}
#endif
}

bool UWeftView::Pointer(EWeftPointer Kind, FVector2D Position, uint32 Buttons, FVector2D Wheel, EWeftModifier Modifiers)
{
#if WITH_WEFT
	if (!Runtime)
	{
		return false;
	}
	TRACE_CPUPROFILER_EVENT_SCOPE(Weft_Pointer);
	weft_pointer_event Event{sizeof(Event)};
	Event.kind = uint32(Kind);
	Event.x = float(Position.X);
	Event.y = float(Position.Y);
	Event.buttons = Buttons;
	Event.modifiers = uint32(Modifiers) & (WEFT_MODIFIER_SHIFT | WEFT_MODIFIER_CONTROL | WEFT_MODIFIER_ALT | WEFT_MODIFIER_META);
	Event.wheel_x = float(Wheel.X);
	Event.wheel_y = float(Wheel.Y);
	uint32 Prevented = 0;
	weft_view_pointer(Runtime, Handle(RuntimeId, Slot, Generation), &Event, &Prevented);
	return Prevented != 0;
#else
	return false;
#endif
}

bool UWeftView::HitTest(FVector2D Position, FString& OutCursor) const
{
	OutCursor = TEXT("auto");
#if WITH_WEFT
	if (!Runtime)
	{
		return false;
	}
	TRACE_CPUPROFILER_EVENT_SCOPE(Weft_HitTest);
	weft_hit Hit{sizeof(Hit)};
	if (weft_view_hit_test(Runtime, Handle(RuntimeId, Slot, Generation), float(Position.X), float(Position.Y), &Hit) != WEFT_OK)
	{
		return false;
	}
	OutCursor = UTF8_TO_TCHAR(Hit.cursor);
	return Hit.element != 0;
#else
	return false;
#endif
}

EWeftInput UWeftView::Key(bool bDown, const FString& Code, const FString& Value, EWeftModifier Modifiers, bool bRepeat,
	bool bIme)
{
#if WITH_WEFT
	if (!Runtime || (Code.IsEmpty() && !bIme))
	{
		return EWeftInput::None;
	}
	const FTCHARToUTF8 CodeUtf8(*Code);
	const FTCHARToUTF8 ValueUtf8(*Value);
	char Typed[32];
	const char* KeyValue = !Value.IsEmpty() ? ValueUtf8.Get()
		: weft_keyboard_key(CodeUtf8.Get(), size_t(CodeUtf8.Length()), uint32(Modifiers), Typed) == WEFT_OK ? Typed
		: "Unidentified";
	return KeyUtf8(bDown, CodeUtf8.Get(), KeyValue, uint32(Modifiers), bRepeat, bIme);
#else
	return EWeftInput::None;
#endif
}

EWeftInput UWeftView::Key(const FKeyEvent& Event, bool bDown)
{
#if WITH_WEFT
	if (Runtime && Event.GetKeyCode() == ProcessKey)
	{
		// The input method took the key: Windows names no physical key for it (Chrome's code comes from the scan code,
		// which Slate does not give). Its keyup, if one comes as VK_PROCESSKEY too, is dropped.
		return bDown ? KeyUtf8(true, "", "Process", ModifiersOf(Event, "", true), Event.IsRepeat(), true) : EWeftInput::None;
	}
	char Code[32];
	if (!Runtime || weft_keyboard_code(Event.GetKeyCode(), Code) != WEFT_OK)
	{
		return EWeftInput::None;
	}
	const uint32 Modifiers = ModifiersOf(Event, Code, bDown);
	char Typed[32];
	const char* Value =
		weft_keyboard_key(Code, size_t(FCStringAnsi::Strlen(Code)), Modifiers, Typed) == WEFT_OK ? Typed : "Unidentified";
	return KeyUtf8(bDown, Code, Value, Modifiers, bDown && Event.IsRepeat());
#else
	return EWeftInput::None;
#endif
}

EWeftInput UWeftView::KeyUtf8(bool bDown, const char* Code, const char* Value, uint32 Modifiers, bool bRepeat, bool bIme)
{
#if WITH_WEFT
	TRACE_CPUPROFILER_EVENT_SCOPE(Weft_Key);
	weft_key_event Event{sizeof(Event)};
	Event.kind = bDown ? WEFT_KEY_DOWN : WEFT_KEY_UP;
	Event.key = Value;
	Event.key_size = size_t(FCStringAnsi::Strlen(Value));
	Event.code = Code;
	Event.code_size = size_t(FCStringAnsi::Strlen(Code));
	Event.modifiers = Modifiers;
	Event.flags = (bRepeat ? WEFT_KEY_REPEAT : 0u) | (bIme ? WEFT_KEY_IME : 0u);
	uint32 Handled = 0;
	if (weft_view_key(Runtime, Handle(RuntimeId, Slot, Generation), &Event, &Handled) != WEFT_OK)
	{
		return EWeftInput::None;
	}
	return EWeftInput(Handled);
#else
	return EWeftInput::None;
#endif
}

EWeftInput UWeftView::Text(const FString& Typed)
{
#if WITH_WEFT
	if (!Runtime || Typed.IsEmpty())
	{
		return EWeftInput::None;
	}
	TRACE_CPUPROFILER_EVENT_SCOPE(Weft_Text);
	const FTCHARToUTF8 Utf8(*Typed);
	uint32 Handled = 0;
	if (weft_view_text(Runtime, Handle(RuntimeId, Slot, Generation), Utf8.Get(), size_t(Utf8.Length()), &Handled) != WEFT_OK)
	{
		return EWeftInput::None;
	}
	return EWeftInput(Handled);
#else
	return EWeftInput::None;
#endif
}

void UWeftView::SetFocus(bool bFocused)
{
#if WITH_WEFT
	if (Runtime)
	{
		Succeeded(weft_view_set_focus(Runtime, Handle(RuntimeId, Slot, Generation), bFocused ? 1u : 0u), TEXT("set the focus"));
	}
#endif
}

EWeftInput UWeftView::Gamepad(const FWeftGamepadState& State)
{
#if WITH_WEFT
	if (!Runtime)
	{
		return EWeftInput::None;
	}
	TRACE_CPUPROFILER_EVENT_SCOPE(Weft_Gamepad);
	static_assert(UE_ARRAY_COUNT(State.Buttons) == WEFT_GAMEPAD_BUTTONS && UE_ARRAY_COUNT(State.Axes) == WEFT_GAMEPAD_AXES);
	const FTCHARToUTF8 Id(*State.Id);
	weft_gamepad_state Out{sizeof(Out)};
	Out.index = uint32(State.Index);
	Out.connected = State.bConnected ? 1u : 0u;
	Out.pressed = State.Pressed;
	Out.id = Id.Get();
	Out.id_size = size_t(Id.Length());
	FMemory::Memcpy(Out.buttons, State.Buttons, sizeof(Out.buttons));
	FMemory::Memcpy(Out.axes, State.Axes, sizeof(Out.axes));
	uint32 Handled = 0;
	if (!Succeeded(weft_view_gamepad(Runtime, Handle(RuntimeId, Slot, Generation), &Out, &Handled), TEXT("give a gamepad")))
	{
		return EWeftInput::None;
	}
	return EWeftInput(Handled);
#else
	return EWeftInput::None;
#endif
}

FWeftTextInput UWeftView::GetTextInput() const
{
	FWeftTextInput Out;
#if WITH_WEFT
	weft_text_input Input{sizeof(Input)};
	if (!Runtime || weft_view_text_input(Runtime, Handle(RuntimeId, Slot, Generation), &Input) != WEFT_OK)
	{
		return Out;
	}
	Out.bActive = Input.active != 0;
	Out.bComposing = Input.composing != 0;
	Out.bPassword = Input.password != 0;
	Out.Caret = FBox2D(FVector2D(Input.caret_x, Input.caret_y),
		FVector2D(Input.caret_x + Input.caret_width, Input.caret_y + Input.caret_height));
	Out.Field = FBox2D(FVector2D(Input.field_x, Input.field_y),
		FVector2D(Input.field_x + Input.field_width, Input.field_y + Input.field_height));
#endif
	return Out;
}

bool UWeftView::Compose(const FString& Text, int32 Cursor)
{
#if WITH_WEFT
	if (!Runtime)
	{
		return false;
	}
	const FTCHARToUTF8 Utf8(*Text);
	// The caret as a UTF-8 offset in the text.
	const uint32 At = Cursor < 0 || Cursor >= Text.Len() ? uint32(Utf8.Length())
		: uint32(FTCHARToUTF8(*Text.Left(Cursor)).Length());
	uint32 Taken = 0;
	return weft_view_composition(Runtime, Handle(RuntimeId, Slot, Generation), Utf8.Get(), size_t(Utf8.Length()), At, At,
			   &Taken) == WEFT_OK &&
		Taken != 0;
#else
	return false;
#endif
}

bool UWeftView::CommitText(const FString& Text)
{
#if WITH_WEFT
	if (!Runtime || Text.IsEmpty())
	{
		return false;
	}
	const FTCHARToUTF8 Utf8(*Text);
	uint32 Taken = 0;
	return weft_view_commit_text(Runtime, Handle(RuntimeId, Slot, Generation), Utf8.Get(), size_t(Utf8.Length()), &Taken) ==
			   WEFT_OK &&
		Taken != 0;
#else
	return false;
#endif
}

TSharedPtr<FWeftFramePacket, ESPMode::ThreadSafe> UWeftView::MakePacket() const
{
#if WITH_WEFT
	if (!Runtime)
	{
		return nullptr;
	}
	TRACE_CPUPROFILER_EVENT_SCOPE(Weft_MakePacket);
	const weft_view_handle View = Handle(RuntimeId, Slot, Generation);
	weft_view_frame* Frame = nullptr;
	if (weft_view_acquire_frame(Runtime, View, &Frame) != WEFT_OK)
	{
		return nullptr;
	}
	TSharedPtr<FWeftFramePacket, ESPMode::ThreadSafe> Packet = MakeShared<FWeftFramePacket, ESPMode::ThreadSafe>();
	Packet->Runtime = Runtime;
	Packet->View = View;
	Packet->Frame = Frame;
	Packet->Anchors.Reserve(Anchors.Num());
	for (const FWeftAnchor& Anchor : Anchors)
	{
		const uint32 Flags = (Anchor.bHidden ? WEFT_ANCHOR_HIDDEN : 0u) | (Anchor.bBehind ? WEFT_ANCHOR_BEHIND : 0u);
		Packet->Anchors.Add({uint64(Anchor.Key), float(Anchor.Position.X), float(Anchor.Position.Y), Anchor.Scale,
			Anchor.Depth, Flags, 0});
	}
	return Packet;
#else
	return nullptr;
#endif
}
