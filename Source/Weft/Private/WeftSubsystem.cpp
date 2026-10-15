#include "WeftSubsystem.h"

#include "Engine/Engine.h"
#include "Misc/Paths.h"
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

bool UWeftSubsystem::AddFont(const FString& Family, const FString& File, int32 Weight, bool bItalic, int32 FaceIndex)
{
#if WITH_WEFT
	if (!Runtime)
	{
		return false;
	}
	const FTCHARToUTF8 Name(*Family);
	const FTCHARToUTF8 Path(*FPaths::ConvertRelativePathToFull(File));
	weft_font_face Face{sizeof(Face)};
	Face.weight = uint32(FMath::Clamp(Weight, 1, 1000));
	Face.italic = bItalic ? 1 : 0;
	Face.face_index = uint32(FMath::Max(FaceIndex, 0));
	Face.family = Name.Get();
	Face.family_size = size_t(Name.Length());
	Face.path = Path.Get();
	Face.path_size = size_t(Path.Length());
	if (const weft_result Result = weft_runtime_add_font(Runtime, &Face); Result != WEFT_OK)
	{
		UE_LOG(LogWeft, Warning, TEXT("Weft: font %s (%s) not added: %s"), *Family, *File, UTF8_TO_TCHAR(weft_result_string(Result)));
		return false;
	}
	return true;
#else
	return false;
#endif
}

bool UWeftSubsystem::AddFallbackFamily(const FString& Family)
{
#if WITH_WEFT
	if (!Runtime)
	{
		return false;
	}
	const FTCHARToUTF8 Name(*Family);
	return weft_runtime_add_fallback_family(Runtime, Name.Get(), size_t(Name.Length())) == WEFT_OK;
#else
	return false;
#endif
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
