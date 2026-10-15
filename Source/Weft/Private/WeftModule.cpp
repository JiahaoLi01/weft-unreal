#include "WeftModule.h"

#include "HAL/PlatformProcess.h"
#include "Interfaces/IPluginManager.h"
#include "Misc/Paths.h"
#include "Modules/ModuleManager.h"

#if WITH_WEFT
extern "C" const char* weft_unreal_build();
#endif

DEFINE_LOG_CATEGORY(LogWeft);
IMPLEMENT_MODULE(FWeftModule, Weft)

void FWeftModule::StartupModule()
{
#if WITH_WEFT
	const TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("Weft"));
	const FString Dll = FPaths::Combine(Plugin->GetBaseDir(), TEXT("Binaries/ThirdParty/Weft"),
		FPlatformProcess::GetBinariesSubdirectory(), TEXT("weft.dll"));
	Library = FPlatformProcess::GetDllHandle(*Dll);
	if (!Library)
	{
		UE_LOG(LogWeft, Error, TEXT("Weft: %s did not load; views draw nothing."), *Dll);
		return;
	}
	UE_LOG(LogWeft, Log, TEXT("Weft: %s (weft %s)"), *Dll, UTF8_TO_TCHAR(weft_unreal_build()));
#else
	UE_LOG(LogWeft, Warning, TEXT("Weft: built without the weft library (run Plugins/Weft/Scripts/BuildWeft.ps1); views draw nothing."));
#endif
}

void FWeftModule::ShutdownModule()
{
	if (Library)
	{
		FPlatformProcess::FreeDllHandle(Library);
		Library = nullptr;
	}
}

FWeftModule& FWeftModule::Get()
{
	return FModuleManager::LoadModuleChecked<FWeftModule>(TEXT("Weft"));
}

bool FWeftModule::IsAvailable()
{
	const FWeftModule* Module = FModuleManager::GetModulePtr<FWeftModule>(TEXT("Weft"));
	return Module && Module->Library;
}
