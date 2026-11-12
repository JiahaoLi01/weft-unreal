#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleInterface.h"

/** The plugin: its DLL, views, widgets and renderer. */
WEFT_API DECLARE_LOG_CATEGORY_EXTERN(LogWeft, Log, All);
/** What the pages say: their exceptions, the styles their scene cannot take, and their console (pages with the web
 *  platform), each line after the page's name ("[Settings] ..."). */
WEFT_API DECLARE_LOG_CATEGORY_EXTERN(LogWeftPage, Log, All);

/** Loads weft's DLL (Binaries/ThirdParty/Weft/<platform>/<build>) when the plugin starts: the dev build where
 *  WITH_WEFT_DEVTOOLS is 1, the release build otherwise (WeftLibrary.Build.cs). */
class WEFT_API FWeftModule : public IModuleInterface
{
public:
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;

	static FWeftModule& Get();
	/** Whether weft runs here: the plugin was built with it and its DLL loaded. */
	static bool IsAvailable();

private:
	void* Library = nullptr;
};
