#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleInterface.h"

WEFT_API DECLARE_LOG_CATEGORY_EXTERN(LogWeft, Log, All);

/** Loads weft's DLL (Binaries/ThirdParty/Weft/<platform>) when the plugin starts. */
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
