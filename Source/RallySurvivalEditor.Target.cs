using UnrealBuildTool;
public class RallySurvivalEditorTarget : TargetRules
{
    public RallySurvivalEditorTarget(TargetInfo Target) : base(Target)
    {
        Type = TargetType.Editor;
        DefaultBuildSettings = BuildSettingsVersion.V7;
        IncludeOrderVersion = EngineIncludeOrderVersion.Unreal5_8;
        ExtraModuleNames.Add("RallySurvival");
    }
}
