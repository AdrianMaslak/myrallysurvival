using UnrealBuildTool;
public class RallySurvival : ModuleRules
{
    public RallySurvival(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
        // Each file compiles on its own: several keep file-local helpers of the same name (AssemblyOffset).
        bUseUnity = false;
        PublicDependencyModuleNames.AddRange(new string[] {
            "Core", "CoreUObject", "Engine", "InputCore", "EnhancedInput",
            "Chaos", "PhysicsCore"
        });
        // Runtime mesh editing for crash dents.
        PrivateDependencyModuleNames.AddRange(new string[] {
            "GeometryCore", "GeometryFramework", "MeshConversionEngineTypes"
        });
        // Procedural sound (docs/SOUND.md).
        PublicDependencyModuleNames.Add("AudioMixer");
    }
}
