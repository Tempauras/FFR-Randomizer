using UnrealBuildTool;

public class FFRSGameTarget : TargetRules
{
	public FFRSGameTarget(TargetInfo Target) : base(Target)
	{
		Type = TargetType.Game;
		DefaultBuildSettings = BuildSettingsVersion.V5;
		IncludeOrderVersion = EngineIncludeOrderVersion.Unreal5_6;
		ExtraModuleNames.Add("FFRSModTools");
		ExtraModuleNames.Add("mucha");
	}
}
