using UnrealBuildTool;

public class ZombiengineeringTarget : TargetRules
{
	public ZombiengineeringTarget(TargetInfo Target) : base(Target)
	{
		Type = TargetType.Game;
		DefaultBuildSettings = BuildSettingsVersion.V5;

		// ✅ Do NOT use Unique with an installed engine
		// BuildEnvironment = TargetBuildEnvironment.Unique;   // REMOVE this

		// ✅ Allow per-target property overrides even with shared env
		bOverrideBuildEnvironment = true;

		// Only affect Shipping
		if (Target.Configuration == UnrealTargetConfiguration.Shipping)
		{
			// BuildEnvironment = TargetBuildEnvironment.Unique;
			bUseLoggingInShipping = true;     // keep UE_LOG in Shipping
		}
	}
}