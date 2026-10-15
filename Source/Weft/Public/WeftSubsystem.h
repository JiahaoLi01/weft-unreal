#pragma once

#include "CoreMinimal.h"
#include "Subsystems/EngineSubsystem.h"

#include "WeftSubsystem.generated.h"

class FWeftRenderer;
class UWeftView;
struct weft_runtime;

/**
 * The weft runtime of the engine: the Views (UWeftView) on the game thread, which owns them, and the renderer that
 * draws their frames with weft's backend of the running RHI (one renderer draws the Views of one runtime).
 */
UCLASS()
class WEFT_API UWeftSubsystem : public UEngineSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	static UWeftSubsystem* Get();

	/** A new View of logical size 1 x 1 until its widget sizes it; null when weft does not run here. */
	UFUNCTION(BlueprintCallable, Category = "Weft")
	UWeftView* CreateView();

	bool IsAvailable() const { return Runtime != nullptr; }

	/** The render side, shared with the widgets' draws. */
	TSharedPtr<FWeftRenderer, ESPMode::ThreadSafe> GetRenderer() const { return Renderer; }

private:
	weft_runtime* Runtime = nullptr;
	TSharedPtr<FWeftRenderer, ESPMode::ThreadSafe> Renderer;
	TArray<TWeakObjectPtr<UWeftView>> Views;
};
