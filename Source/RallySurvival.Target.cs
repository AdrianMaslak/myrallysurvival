using UnrealBuildTool;
public class RallySurvivalTarget : TargetRules
{
    public RallySurvivalTarget(TargetInfo Target) : base(Target)
    {
        Type = TargetType.Game;
        DefaultBuildSettings = BuildSettingsVersion.V7;
        IncludeOrderVersion = EngineIncludeOrderVersion.Unreal5_8;
        ExtraModuleNames.Add("RallySurvival");
    }
}
