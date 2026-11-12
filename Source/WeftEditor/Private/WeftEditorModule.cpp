#include "Framework/Application/SlateApplication.h"
#include "Framework/Docking/TabManager.h"
#include "HAL/IConsoleManager.h"
#include "Modules/ModuleManager.h"
#include "Styling/AppStyle.h"
#include "SWeftDebugPanel.h"
#include "Widgets/Docking/SDockTab.h"
#include "WorkspaceMenuStructure.h"
#include "WorkspaceMenuStructureModule.h"

#define LOCTEXT_NAMESPACE "WeftEditor"

#if WITH_WEFT_DEVTOOLS
namespace
{
	const FName WeftPanelTab(TEXT("WeftPanel"));

	FAutoConsoleCommand PanelCommand(TEXT("weft.Panel"),
		TEXT("Opens the editor's Weft panel (Window > Log > Weft): the weft views, what they cost, and weft's developer tools."),
		FConsoleCommandDelegate::CreateLambda([] { FGlobalTabmanager::Get()->TryInvokeTab(WeftPanelTab); }));
} // namespace
#endif

/** The plugin in the editor: the Weft panel, beside the Output Log (Window > Log), where weft's dev build runs. */
class FWeftEditorModule : public IModuleInterface
{
public:
	virtual void StartupModule() override
	{
#if WITH_WEFT_DEVTOOLS
		FGlobalTabmanager::Get()
			->RegisterNomadTabSpawner(WeftPanelTab, FOnSpawnTab::CreateLambda([](const FSpawnTabArgs&)
			{
				return SNew(SDockTab).TabRole(ETabRole::NomadTab)[SNew(SWeftDebugPanel)];
			}))
			.SetDisplayName(LOCTEXT("PanelTitle", "Weft"))
			.SetTooltipText(LOCTEXT("PanelTooltip", "The weft views: their pages, what they cost, their errors, and weft's developer tools."))
			.SetGroup(WorkspaceMenu::GetMenuStructure().GetDeveloperToolsLogCategory())
			.SetIcon(FSlateIcon(FAppStyle::GetAppStyleSetName(), "LevelEditor.Tabs.Debug"));
#endif
	}

	virtual void ShutdownModule() override
	{
#if WITH_WEFT_DEVTOOLS
		if (FSlateApplication::IsInitialized())
		{
			FGlobalTabmanager::Get()->UnregisterNomadTabSpawner(WeftPanelTab);
		}
#endif
	}
};

IMPLEMENT_MODULE(FWeftEditorModule, WeftEditor)

#undef LOCTEXT_NAMESPACE
