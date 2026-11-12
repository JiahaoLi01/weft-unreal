using UnrealBuildTool;

// Weft views in Unreal: pages loaded into weft Views on the game thread, drawn by weft's backend of the running RHI
// (D3D12 now) into Slate's output where the view's widget is.
public class Weft : ModuleRules
{
	public Weft(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
		PublicDependencyModuleNames.AddRange(new[] { "Core", "CoreUObject", "Engine", "Slate", "SlateCore" });
		PrivateDependencyModuleNames.AddRange(new[]
		{
			"ApplicationCore", "InputCore", "Json", "Projects", "RenderCore", "RHI", "Renderer", "WeftLibrary"
		});
		if (Target.Platform == UnrealTargetPlatform.Win64)
		{
			// The D3D12 RHI's device, command lists and resources, which weft's D3D12 backend records with.
			PrivateDependencyModuleNames.Add("D3D12RHI");
			AddEngineThirdPartyPrivateStaticDependencies(Target, "DX12");
		}
		// Whether the plugin runs weft's dev build (WeftLibrary.Build.cs): its developer tools, and the plugin's debug
		// tools with them (weft.DevTools, weft.Views, weft.Debug, the editor's Weft panel). 0 leaves them out of the
		// binary. Public, so that every module that includes the plugin's headers sees the same.
		bool bDevTools = WeftLibrary.UsesDevTools(Target);
		bool bWithDevTools = bDevTools && WeftLibrary.IsBuilt(Target, PluginDirectory, bDevTools);
		PublicDefinitions.Add("WITH_WEFT_DEVTOOLS=" + (bWithDevTools ? "1" : "0"));
		if (bWithDevTools)
		{
			// The developer tools' test reads what the socket lists (Tests/WeftDevToolsTests.cpp).
			PrivateDependencyModuleNames.Add("Sockets");
		}
	}
}
