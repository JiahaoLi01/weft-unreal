#include "WeftSubsystem.h"

#include "Engine/Engine.h"
#include "Render/WeftRenderer.h"
#include "RenderingThread.h"
#include "WeftModule.h"
#include "WeftView.h"

#if WITH_WEFT
#include "weft/weft.h"
#endif

void UWeftSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
#if WITH_WEFT
	if (!FWeftModule::IsAvailable())
	{
		return;
	}
	// The game thread owns the runtime: its Views' pages run there. The platform's fonts, with their generic families
	// and fallbacks (CJK included).
	if (const weft_result Result = weft_runtime_create(nullptr, &Runtime); Result != WEFT_OK)
	{
		UE_LOG(LogWeft, Error, TEXT("Weft: no runtime: %s"), UTF8_TO_TCHAR(weft_result_string(Result)));
		Runtime = nullptr;
		return;
	}
	Renderer = MakeShared<FWeftRenderer, ESPMode::ThreadSafe>();
#endif
}

void UWeftSubsystem::Deinitialize()
{
#if WITH_WEFT
	if (Runtime)
	{
		for (const TWeakObjectPtr<UWeftView>& View : Views)
		{
			if (UWeftView* Alive = View.Get())
			{
				Alive->Close();
			}
		}
		Views.Empty();
		// What the render thread still holds of the Views goes first, then the renderer (after the GPU is done with
		// it), then the runtime.
		FlushRenderingCommands();
		if (Renderer)
		{
			Renderer->Shutdown();
			Renderer.Reset();
		}
		if (const weft_result Result = weft_runtime_destroy(Runtime); Result != WEFT_OK)
		{
			UE_LOG(LogWeft, Warning, TEXT("Weft: the runtime stays: %s"), UTF8_TO_TCHAR(weft_result_string(Result)));
		}
		Runtime = nullptr;
	}
#endif
	Super::Deinitialize();
}

UWeftSubsystem* UWeftSubsystem::Get()
{
	return GEngine ? GEngine->GetEngineSubsystem<UWeftSubsystem>() : nullptr;
}

UWeftView* UWeftSubsystem::CreateView()
{
	if (!Runtime)
	{
		return nullptr;
	}
	UWeftView* View = NewObject<UWeftView>(this);
	if (!View->Open(Runtime))
	{
		return nullptr;
	}
	Views.RemoveAll([](const TWeakObjectPtr<UWeftView>& Old) { return !Old.IsValid(); });
	Views.Add(View);
	return View;
}
