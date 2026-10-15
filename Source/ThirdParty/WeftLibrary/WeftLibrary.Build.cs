using System.IO;
using UnrealBuildTool;

// The weft runtime (https://github.com/JiahaoLi01/weft) as one DLL behind its C ABI: headers, import library and DLL,
// which Scripts/BuildWeft.ps1 builds from a weft checkout. Without them the plugin builds and runs without UI
// (WITH_WEFT=0), so a project builds before weft is.
public class WeftLibrary : ModuleRules
{
	public WeftLibrary(ReadOnlyTargetRules Target) : base(Target)
	{
		Type = ModuleType.External;
		string Include = Path.Combine(ModuleDirectory, "include");
		bool bBuilt = false;
		if (Target.Platform == UnrealTargetPlatform.Win64)
		{
			string Library = Path.Combine(ModuleDirectory, "lib", "Win64", "weft.lib");
			string Dll = Path.Combine(PluginDirectory, "Binaries", "ThirdParty", "Weft", "Win64", "weft.dll");
			bBuilt = File.Exists(Path.Combine(Include, "weft", "weft.h")) && File.Exists(Library) && File.Exists(Dll);
			if (bBuilt)
			{
				PublicSystemIncludePaths.Add(Include);
				PublicAdditionalLibraries.Add(Library);
				PublicDelayLoadDLLs.Add("weft.dll");
				RuntimeDependencies.Add("$(PluginDir)/Binaries/ThirdParty/Weft/Win64/weft.dll");
			}
		}
		PublicDefinitions.Add("WITH_WEFT=" + (bBuilt ? "1" : "0"));
	}
}
