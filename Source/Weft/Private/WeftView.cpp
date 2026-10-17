#include "WeftView.h"

#include "HAL/PlatformTime.h"
#include "Misc/FileHelper.h"
#include "Render/WeftFramePacket.h"
#include "WeftModule.h"
#include "WeftValue.h"

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

	bool Succeeded(weft_result Result, const TCHAR* What)
	{
		if (Result != WEFT_OK)
		{
			UE_LOG(LogWeft, Warning, TEXT("Weft: %s: %s"), What, UTF8_TO_TCHAR(weft_result_string(Result)));
			return false;
		}
		return true;
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
		(Page.bWebPlatform ? WEFT_PAGE_WEB_PLATFORM : 0u);
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

void UWeftView::Advance()
{
#if WITH_WEFT
	if (!Runtime)
	{
		return;
	}
	const weft_view_handle View = Handle(RuntimeId, Slot, Generation);
	Succeeded(weft_view_advance(Runtime, View, FPlatformTime::Seconds() - StartSeconds), TEXT("advance a view"));
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

bool UWeftView::Pointer(EWeftPointer Kind, FVector2D Position, uint32 Buttons, FVector2D Wheel)
{
#if WITH_WEFT
	if (!Runtime)
	{
		return false;
	}
	weft_pointer_event Event{sizeof(Event)};
	Event.kind = uint32(Kind);
	Event.x = float(Position.X);
	Event.y = float(Position.Y);
	Event.buttons = Buttons;
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

TSharedPtr<FWeftFramePacket, ESPMode::ThreadSafe> UWeftView::MakePacket() const
{
#if WITH_WEFT
	if (!Runtime)
	{
		return nullptr;
	}
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
