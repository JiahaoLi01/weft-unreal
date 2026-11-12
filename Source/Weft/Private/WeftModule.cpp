#include "WeftModule.h"

#include "HAL/PlatformProcess.h"
#include "Interfaces/IPluginManager.h"
#include "Misc/Paths.h"
#include "Modules/ModuleManager.h"

#if WITH_WEFT
#include "weft/weft.h"

extern "C" const char* weft_unreal_build();
#endif

DEFINE_LOG_CATEGORY(LogWeft);
DEFINE_LOG_CATEGORY(LogWeftPage);
IMPLEMENT_MODULE(FWeftModule, Weft)

void FWeftModule::StartupModule()
{
#if WITH_WEFT
	// The build the plugin was compiled for (WeftLibrary.Build.cs): the two have the same exports.
	const TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("Weft"));
	const FString Dll = FPaths::Combine(Plugin->GetBaseDir(), TEXT("Binaries/ThirdParty/Weft"),
		FPlatformProcess::GetBinariesSubdirectory(), WITH_WEFT_DEVTOOLS ? TEXT("Dev") : TEXT("Release"), TEXT("weft.dll"));
	Library = FPlatformProcess::GetDllHandle(*Dll);
	if (!Library)
	{
		UE_LOG(LogWeft, Error, TEXT("Weft: %s did not load; views draw nothing."), *Dll);
		return;
	}
	UE_LOG(LogWeft, Log, TEXT("Weft: %s (weft %s)"), *Dll, UTF8_TO_TCHAR(weft_unreal_build()));
#if WITH_WEFT_DEVTOOLS
	// The release build in the dev build's place would answer every developer tool with WEFT_UNAVAILABLE.
	weft_build_info Build{sizeof(Build)};
	if (weft_get_build_info(&Build) != WEFT_OK || (Build.flags & WEFT_BUILD_DEVTOOLS) == 0)
	{
		UE_LOG(LogWeft, Warning, TEXT("Weft: %s is weft's release build, without its developer tools: run Scripts/BuildWeft.ps1 again."), *Dll);
	}
#endif
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
