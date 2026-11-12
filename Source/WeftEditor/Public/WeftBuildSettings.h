#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"

#include "WeftBuildSettings.generated.h"

/**
 * How a game is built with weft (Project Settings > Plugins > Weft UI; the project's Config/DefaultEngine.ini). Read when
 * the game is built (WeftLibrary.Build.cs), not when it runs: change it, then build.
 */
UCLASS(Config = Engine, DefaultConfig, meta = (DisplayName = "Weft UI"))
class WEFTEDITOR_API UWeftBuildSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	virtual FName GetCategoryName() const override { return TEXT("Plugins"); }

	/**
	 * Shipping builds take weft's dev build, with its developer tools (Chrome DevTools on the pages: -WeftDevTools on the
	 * game's command line), and the plugin's debug tools (-WeftDebug), instead of weft's release build, which has none of
	 * them. Off, a Shipping build holds nothing of the tools. One build asks for them on UnrealBuildTool's command line
	 * instead: -ini:Engine:[/Script/WeftEditor.WeftBuildSettings]:bDevToolsInShipping=True (UAT: -ubtargs="...").
	 */
	UPROPERTY(Config, EditAnywhere, Category = "Shipping", meta = (DisplayName = "Developer Tools in Shipping"))
	bool bDevToolsInShipping = false;
};
