#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "WeftTypes.h"

#include "WeftView.generated.h"

struct weft_runtime;
struct FWeftFramePacket;

/** How a pointer event reaches a page (weft_pointer_kind). */
enum class EWeftPointer : uint8
{
	Move,
	Down,
	Up,
	Wheel,
	Leave
};

/** A message of the page: weft.engine.send (CallId 0) or weft.engine.call (complete it with CompleteCall). */
DECLARE_MULTICAST_DELEGATE_ThreeParams(FOnWeftMessage, const FString& /*Name*/, const FString& /*JsonValue*/, uint64 /*CallId*/);

/** What a diagnostic of the page is (weft_diagnostic_kind): an exception of its scripts, its weft.engine handlers or its
 *  animation frames, a scene its styles cannot make, or a line of its console (the web platform's), by level. */
enum class EWeftDiagnostic : uint8
{
	Script = 1,
	Handler,
	Frame,
	Scene,
	Console,
	ConsoleWarning,
	ConsoleError
};

/** A diagnostic of the page, after the log has it. */
DECLARE_MULTICAST_DELEGATE_TwoParams(FOnWeftDiagnostic, EWeftDiagnostic /*Kind*/, const FString& /*Message*/);

/**
 * One weft View: a page, its document, script and scene, owned by the game thread (UWeftSubsystem::CreateView). The
 * engine talks to the page with events, view models and anchors; the page answers with messages. A widget
 * (SWeftViewport) sizes it, advances it every frame and draws it.
 */
UCLASS(BlueprintType, Transient)
class WEFT_API UWeftView : public UObject
{
	GENERATED_BODY()

public:
	/** Loads the page in a folder (FWeftPage::FromDirectory). False, with the page's diagnostics in the log, when it
	 *  does not load. */
	UFUNCTION(BlueprintCallable, Category = "Weft")
	bool LoadDirectory(const FString& Directory);
	bool Load(const FWeftPage& Page);

	/** An event to the page's weft.engine.on(Name) handlers, its value JSON text. bLatest replaces an undelivered
	 *  event of the name instead of queueing another. */
	UFUNCTION(BlueprintCallable, Category = "Weft")
	bool Emit(const FString& Name, const FString& Json, bool bLatest = false);

	/** Merges fields (a JSON object of field names to values) into the view model weft.engine.model(Model). */
	UFUNCTION(BlueprintCallable, Category = "Weft")
	bool SetModel(const FString& Model, const FString& FieldsJson);

	/** The anchors of the frames drawn from now on (all of them: a key left out is missing). */
	UFUNCTION(BlueprintCallable, Category = "Weft")
	void SetAnchors(const TArray<FWeftAnchor>& Anchors);

	/** Settles a weft.engine.call: resolves it with the JSON value, or rejects it. */
	bool CompleteCall(uint64 CallId, bool bSucceeded, const FString& Json);

	/** The view's size in logical px (CSS px) and the device px each spans. */
	void Resize(FVector2D LogicalSize, float DeviceScale);
	FVector2D GetLogicalSize() const { return Size; }
	float GetDeviceScale() const { return Scale; }

	/**
	 * CSS px per unit of the widget that shows the view (1 by default): at 2 the page lays out in half the widget's
	 * units and every CSS px spans twice the device px, as the page zoomed by 2 (a UI scale setting), text drawn at
	 * the device's resolution. The widget sizes the view and maps the pointer with it.
	 */
	UFUNCTION(BlueprintCallable, Category = "Weft")
	void SetContentScale(float InContentScale);
	UFUNCTION(BlueprintPure, Category = "Weft")
	float GetContentScale() const { return ContentScale; }

	/**
	 * The page's localStorage (pages with the web platform): kept across the View's page loads, for the game to save
	 * and restore. SetStorageItem sets an item (an empty Value with bRemove removes it); before the page loads, it
	 * restores what was saved. GetStorage hands the items over when they changed since Revision (0: in any case) and
	 * writes their revision; false when they did not change.
	 */
	bool SetStorageItem(const FString& Key, const FString& Value, bool bRemove = false);
	bool ClearStorage();
	bool GetStorage(uint64& Revision, TMap<FString, FString>& OutItems) const;

	/** Runs the page up to now (its events, view models, tasks, animation frames and scene), then hands its messages to
	 *  OnMessage and its diagnostics to the log. */
	void Advance();

	/** A pointer event at a position in logical px; Buttons as MouseEvent.buttons after it. True when a listener
	 *  prevented its default action. */
	bool Pointer(EWeftPointer Kind, FVector2D Position, uint32 Buttons, FVector2D Wheel = FVector2D::ZeroVector);

	/** Whether a page element other than the root takes pointer events at a position (logical px), and the CSS cursor
	 *  it shows there. */
	bool HitTest(FVector2D Position, FString& OutCursor) const;

	/** The frame published last with the anchors set last, for the render thread; null before the first frame. */
	TSharedPtr<FWeftFramePacket, ESPMode::ThreadSafe> MakePacket() const;

	bool IsOpen() const { return Runtime != nullptr; }

	FOnWeftMessage OnMessage;
	FOnWeftDiagnostic OnDiagnostic;

protected:
	virtual void BeginDestroy() override;

private:
	friend class UWeftSubsystem;

	/** A View of runtime of the given size; false when weft refused one. */
	bool Open(weft_runtime* InRuntime);
	/** Destroys the View (the runtime is going). */
	void Close();

	weft_runtime* Runtime = nullptr;
	// weft_view_handle.
	uint64 RuntimeId = 0;
	uint32 Slot = 0;
	uint32 Generation = 0;
	double StartSeconds = 0;
	FVector2D Size = FVector2D(1, 1);
	float Scale = 1.f;
	float ContentScale = 1.f;
	TArray<FWeftAnchor> Anchors;
};
