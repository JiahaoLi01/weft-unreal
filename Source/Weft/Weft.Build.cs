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
	}
}
