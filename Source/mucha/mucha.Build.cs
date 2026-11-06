using UnrealBuildTool;

// A stand-in for the game's own mucha module, holding only the types the randomizer's Blueprints
// call. Blueprints store references as /Script/mucha paths, so in game they reach the real ones.
public class mucha : ModuleRules
{
	public mucha(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"CoreUObject",
			"Engine",
			"GameplayTags",
			"UMG",
			"CommonUI",
			"EnhancedUI",
		});
	}
}
