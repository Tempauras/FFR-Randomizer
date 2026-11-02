using UnrealBuildTool;

public class FFRSEditorTarget : TargetRules
{
	public FFRSEditorTarget(TargetInfo Target) : base(Target)
	{
		Type = TargetType.Editor;
		DefaultBuildSettings = BuildSettingsVersion.V5;
		IncludeOrderVersion = EngineIncludeOrderVersion.Unreal5_6;
		ExtraModuleNames.Add("FFRSModTools");
	}
}
