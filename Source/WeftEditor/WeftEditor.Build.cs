using UnrealBuildTool;

// The plugin in the editor: the Weft panel (Window > Log > Weft: the views, what they cost, weft's developer tools) and
// the project setting that puts weft's developer tools into Shipping (UWeftBuildSettings, which WeftLibrary.Build.cs
// reads when a game is built).
public class WeftEditor : ModuleRules
{
	public WeftEditor(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
		PublicDependencyModuleNames.AddRange(new[] { "Core", "CoreUObject", "DeveloperSettings" });
		PrivateDependencyModuleNames.AddRange(new[]
		{
			"ApplicationCore", "Engine", "InputCore", "Slate", "SlateCore", "Weft", "WorkspaceMenuStructure"
		});
	}
}
