// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class Zombiengineering : ModuleRules
{
	public Zombiengineering(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[] {
			"Core",
			"CoreUObject",
			"Engine",
			"InputCore",
			"EnhancedInput",
			"AIModule",
			"StateTreeModule",
			"GameplayStateTreeModule",
			"UMG",
			"MassEntity",
			"MassCommon",
			"MassMovement",
			"MassNavigation",
			"NavigationSystem",
			"MassActors",
			"DeveloperSettings"
		});

		PrivateDependencyModuleNames.AddRange(new string[]
		{
			"MassSpawner",
			"MassNavigation",
			"MassAIBehavior",
			"StateTreeModule",
			"RHI",
			"RenderCore"
		});
		if (Target.bBuildEditor)
		{
			PrivateDependencyModuleNames.AddRange(new string[]
			{
				"UnrealEd"
				// add other editor-only modules here if needed: "Slate", "SlateCore", "AssetTools", etc.
			});
		}

		PublicIncludePaths.AddRange(new string[] {
			"Zombiengineering"
		});

		// Uncomment if you are using Slate UI
		// PrivateDependencyModuleNames.AddRange(new string[] { "Slate", "SlateCore" });

		// Uncomment if you are using online features
		// PrivateDependencyModuleNames.Add("OnlineSubsystem");

		// To include OnlineSubsystemSteam, add it to the plugins section in your uproject file with the Enabled attribute set to true
	}
}
