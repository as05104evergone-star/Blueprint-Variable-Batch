using UnrealBuildTool;

public class BlueprintVariableBatchEditor : ModuleRules
{
	public BlueprintVariableBatchEditor(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(
			new string[]
			{
				"Core",
				"CoreUObject",
				"Engine",
			});

		PrivateDependencyModuleNames.AddRange(
			new string[]
			{
				"Slate",
				"SlateCore",
				"InputCore",
				"ApplicationCore",
				"UnrealEd",
				"Kismet",
				"KismetWidgets",
				"BlueprintGraph",
				"GraphEditor",
				"ToolMenus",
				"Projects",
				"RHI",
			});
	}
}
