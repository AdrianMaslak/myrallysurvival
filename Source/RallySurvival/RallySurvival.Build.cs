using UnrealBuildTool;
public class RallySurvival : ModuleRules
{
    public RallySurvival(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
        PublicDependencyModuleNames.AddRange(new string[] {
            "Core", "CoreUObject", "Engine", "InputCore", "EnhancedInput",
            "Chaos", "PhysicsCore"
        });
        // Runtime mesh editing for crash dents.
        PrivateDependencyModuleNames.AddRange(new string[] {
            "GeometryCore", "GeometryFramework", "MeshConversionEngineTypes"
        });
    }
}
