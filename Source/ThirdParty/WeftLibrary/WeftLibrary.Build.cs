using System.IO;
using Microsoft.Extensions.Logging;
using UnrealBuildTool;

// The weft runtime (https://github.com/JiahaoLi01/weft) as one DLL behind its C ABI: headers, import library and DLL,
// which Scripts/BuildWeft.ps1 builds from a weft checkout. Without them the plugin builds and runs without UI
// (WITH_WEFT=0), so a project builds before weft is.
//
// weft comes as two DLLs with the same header and exports (weft's docs/devtools.md): the dev build, with weft's
// developer tools in it (off until asked for), and the release build, with none of them. Every configuration but
// Shipping takes the dev build. Shipping takes the release build unless the project asks for the tools in it:
// bDevToolsInShipping=True in [/Script/WeftEditor.WeftBuildSettings] of its Config/DefaultEngine.ini (Project Settings >
// Plugins > Weft UI), or the same given to UnrealBuildTool for one build:
//   -ini:Engine:[/Script/WeftEditor.WeftBuildSettings]:bDevToolsInShipping=True
// (through UAT: -ubtargs="-ini:..."). UnrealBuildTool reads the rules again when the ini file or its own command line
// changes. The Weft module says which build it has in WITH_WEFT_DEVTOOLS (Weft.Build.cs).
public class WeftLibrary : ModuleRules
{
	/** Where the project asks for weft's developer tools in Shipping. */
	const string SettingsSection = "/Script/WeftEditor.WeftBuildSettings";
	const string DevToolsInShippingKey = "bDevToolsInShipping";

	public WeftLibrary(ReadOnlyTargetRules Target) : base(Target)
	{
		Type = ModuleType.External;
		bool bDevTools = UsesDevTools(Target);
		bool bBuilt = IsBuilt(Target, PluginDirectory, bDevTools);
		if (bBuilt)
		{
			PublicSystemIncludePaths.Add(Path.Combine(ModuleDirectory, "include"));
			// Both builds export the same functions: one import library serves them.
			PublicAdditionalLibraries.Add(Path.Combine(ModuleDirectory, "lib", "Win64", "weft.lib"));
			PublicDelayLoadDLLs.Add("weft.dll");
			// The build this target runs with, and not the other, goes with the game.
			RuntimeDependencies.Add("$(PluginDir)/Binaries/ThirdParty/Weft/Win64/" + BuildName(bDevTools) + "/weft.dll");
			if (Target.Configuration == UnrealTargetConfiguration.Shipping && bDevTools)
			{
				Logger.LogInformation("Weft: Shipping with weft's developer tools ({Key}=True)", DevToolsInShippingKey);
			}
		}
		else if (Target.Platform == UnrealTargetPlatform.Win64)
		{
			Logger.LogInformation("Weft: the {Build} DLL is not built (Scripts/BuildWeft.ps1); views draw nothing",
				BuildName(bDevTools));
		}
		PublicDefinitions.Add("WITH_WEFT=" + (bBuilt ? "1" : "0"));
	}

	/** Whether a target takes weft's dev build: every configuration but Shipping, and Shipping when the project asks for
	 *  it (its Engine ini, with UnrealBuildTool's -ini: overrides). */
	public static bool UsesDevTools(ReadOnlyTargetRules Target)
	{
		if (Target.Configuration != UnrealTargetConfiguration.Shipping)
		{
			return true;
		}
		ConfigHierarchy Engine = ConfigCache.ReadHierarchy(ConfigHierarchyType.Engine,
			Target.ProjectFile != null ? Target.ProjectFile.Directory : null, Target.Platform);
		return Engine.GetBool(SettingsSection, DevToolsInShippingKey, out bool bValue) && bValue;
	}

	/** Whether the headers, the import library and the build's DLL are there for the target's platform. */
	public static bool IsBuilt(ReadOnlyTargetRules Target, string PluginDirectory, bool bDevTools)
	{
		if (Target.Platform != UnrealTargetPlatform.Win64)
		{
			return false;
		}
		string Module = Path.Combine(PluginDirectory, "Source", "ThirdParty", "WeftLibrary");
		return File.Exists(Path.Combine(Module, "include", "weft", "weft.h")) &&
			File.Exists(Path.Combine(Module, "lib", "Win64", "weft.lib")) &&
			File.Exists(Path.Combine(PluginDirectory, "Binaries", "ThirdParty", "Weft", "Win64", BuildName(bDevTools), "weft.dll"));
	}

	/** The folder of a build's DLL in Binaries/ThirdParty/Weft/Win64. */
	public static string BuildName(bool bDevTools)
	{
		return bDevTools ? "Dev" : "Release";
	}
}
