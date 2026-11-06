using UnrealBuildTool;

// A stand-in for the game's EnhancedUI module, holding only the types the randomizer's Blueprints
// use. Blueprints store references as /Script/EnhancedUI paths, so in game they reach the real ones.
public class EnhancedUI : ModuleRules
{
	public EnhancedUI(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"CoreUObject",
			"Engine",
			"SlateCore",
			"UMG",
			"CommonUI",
		});
	}
}
