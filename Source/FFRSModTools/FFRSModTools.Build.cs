using UnrealBuildTool;

public class FFRSModTools : ModuleRules
{
	public FFRSModTools(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PrivateDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"CoreUObject",
			"Engine",
			"InputCore",
			"UnrealEd",
			"BlueprintGraph",
			"Kismet",
			"KismetCompiler",
			"UMG",
			"UMGEditor",
			"Slate",
			"SlateCore",
		});
	}
}
