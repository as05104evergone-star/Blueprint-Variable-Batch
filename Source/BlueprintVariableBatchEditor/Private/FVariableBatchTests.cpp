#include "CoreMinimal.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

#include "Editor.h"
#include "Engine/Blueprint.h"
#include "Engine/BlueprintGeneratedClass.h"
#include "FVariableBatchApplier.h"
#include "FVariableBatchCollector.h"
#include "Framework/Docking/TabManager.h"
#include "GameFramework/Actor.h"
#include "BlueprintEditor.h"
#include "BlueprintVariableBatchEditor.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Subsystems/AssetEditorSubsystem.h"
#include "UObject/Package.h"
#include "RHIGlobals.h"
#include "Widgets/Docking/SDockTab.h"

/**
 * 批量改变量类型的核心逻辑测试：临时蓝图 → 收集变量 → 生成计划 → 应用 → 校验类型。
 * 仅覆盖成员变量路径；局部变量与 Slate 交互由手动验收清单覆盖。
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FVariableBatchBatchChangeTypeTest,
	"BlueprintVariableBatch.BatchChangeType",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FVariableBatchBatchChangeTypeTest::RunTest(const FString& Parameters)
{
	const FString PackageName = TEXT("/Temp/BP_VariableBatchAutomationTest");
	UPackage* Package = CreatePackage(*PackageName);
	if (!TestNotNull(TEXT("临时包创建成功"), Package))
	{
		return false;
	}

	UBlueprint* Blueprint = FKismetEditorUtilities::CreateBlueprint(
		AActor::StaticClass(),
		Package,
		TEXT("BP_VariableBatchAutomationTest"),
		BPTYPE_Normal,
		UBlueprint::StaticClass(),
		UBlueprintGeneratedClass::StaticClass());

	if (!TestNotNull(TEXT("临时蓝图创建成功"), Blueprint))
	{
		return false;
	}

	FEdGraphPinType IntType;
	IntType.PinCategory = UEdGraphSchema_K2::PC_Int;

	FEdGraphPinType BoolType;
	BoolType.PinCategory = UEdGraphSchema_K2::PC_Boolean;

	FEdGraphPinType ObjectType;
	ObjectType.PinCategory = UEdGraphSchema_K2::PC_Object;
	ObjectType.PinSubCategoryObject = AActor::StaticClass();

	FBlueprintEditorUtils::AddMemberVariable(Blueprint, TEXT("TestInt"), IntType);
	FBlueprintEditorUtils::AddMemberVariable(Blueprint, TEXT("TestBool"), BoolType);
	FBlueprintEditorUtils::AddMemberVariable(Blueprint, TEXT("TestActor"), ObjectType);

	// 1. 收集变量
	TArray<TSharedPtr<FVariableBatchEntry>> Entries;
	const bool bCollected = FVariableBatchCollector(Blueprint).Collect(Entries);
	TestTrue(TEXT("变量收集成功"), bCollected);
	TestEqual(TEXT("收集到 3 个成员变量"), Entries.Num(), 3);

	// 2. 选中 Bool 与 Object 两个变量，统一改为 int
	TArray<TSharedPtr<FVariableBatchEntry>> Selected;
	for (const TSharedPtr<FVariableBatchEntry>& Entry : Entries)
	{
		if (Entry.IsValid() && Entry->Name != TEXT("TestInt"))
		{
			Selected.Add(Entry);
		}
	}
	TestEqual(TEXT("选中 2 个变量"), Selected.Num(), 2);

	const FVariableBatchPlan Plan = FVariableBatchApplier::BuildPlan(Blueprint, Selected, IntType);
	TestTrue(TEXT("计划生成成功"), Plan.IsValid());
	TestEqual(TEXT("计划中 2 个变量待修改"), Plan.ToChange.Num(), 2);

	// 3. 应用
	const FVariableBatchApplyResult Result = FVariableBatchApplier::ApplyPlan(Blueprint, Plan);
	TestEqual(TEXT("实际修改 2 个变量"), Result.NumChanged, 2);

	// 4. 校验类型与显示名
	for (const FBPVariableDescription& Variable : Blueprint->NewVariables)
	{
		TestEqual(
			FString::Printf(TEXT("变量 %s 已变为 int"), *Variable.VarName.ToString()),
			Variable.VarType.PinCategory,
			UEdGraphSchema_K2::PC_Int);
	}

	// Bool → int 互转后引擎会按新类型重算显示名（FriendlyName），这里只确认它仍是非空且被重新分词。
	const FBPVariableDescription* BoolVariable = Blueprint->NewVariables.FindByPredicate(
		[](const FBPVariableDescription& Variable) { return Variable.VarName == TEXT("TestBool"); });
	if (TestNotNull(TEXT("找到 TestBool 变量"), BoolVariable))
	{
		TestTrue(TEXT("TestBool 的显示名已按 int 重新分词"), BoolVariable->FriendlyName.Contains(TEXT(" ")));
	}

	// 5. 把同一个选择再应用一次，应被判为「无变化」（计划以蓝图里的当前类型为准）
	const FVariableBatchPlan NoChangePlan = FVariableBatchApplier::BuildPlan(Blueprint, Selected, IntType);
	TestFalse(TEXT("再次应用时计划无效（已是目标类型）"), NoChangePlan.IsValid());

	// 清理临时包
	if (Package)
	{
		Package->SetDirtyFlag(false);
	}

	return true;
}

/**
 * Tab 集成测试：打开一个临时蓝图编辑器，确认「变量批量编辑器」Tab 能被框架打开并生成面板。
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FVariableBatchTabSpawnTest,
	"BlueprintVariableBatch.TabSpawn",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

namespace
{
	/** 等待若干帧，给面板的兜底自动打开（FTSTicker 计帧）留出时间。*/
	class FVariableBatchWaitFramesCommand : public IAutomationLatentCommand
	{
	public:
		explicit FVariableBatchWaitFramesCommand(int32 InFrames)
			: RemainingFrames(InFrames)
		{
		}

		virtual bool Update() override
		{
			return --RemainingFrames <= 0;
		}

	private:
		int32 RemainingFrames;
	};

	/** 检查面板 Tab 是否已在蓝图编辑器里出现，随后收尾关闭编辑器。*/
	class FVariableBatchCheckTabCommand : public IAutomationLatentCommand
	{
	public:
		FVariableBatchCheckTabCommand(TWeakObjectPtr<UBlueprint> InBlueprint, FAutomationTestBase* InTest)
			: Blueprint(InBlueprint)
			, Test(InTest)
		{
		}

		virtual bool Update() override
		{
			UAssetEditorSubsystem* AssetEditorSubsystem = GEditor
				? GEditor->GetEditorSubsystem<UAssetEditorSubsystem>()
				: nullptr;

			IAssetEditorInstance* EditorInstance = (AssetEditorSubsystem && Blueprint.IsValid())
				? AssetEditorSubsystem->FindEditorForAsset(Blueprint.Get(), false)
				: nullptr;

			FBlueprintEditor* BlueprintEditor = static_cast<FBlueprintEditor*>(EditorInstance);
			if (Test)
			{
				Test->TestNotNull(TEXT("蓝图编辑器实例仍然有效"), BlueprintEditor);
			}

			if (BlueprintEditor && BlueprintEditor->GetTabManager().IsValid())
			{
				const TSharedPtr<FTabManager> TabManager = BlueprintEditor->GetTabManager();
				const FTabId BatchTabId(FBlueprintVariableBatchEditorModule::GetTabId());

				const bool bAutoOpened = TabManager->FindExistingLiveTab(BatchTabId).IsValid();
				if (Test)
				{
					Test->TestTrue(TEXT("打开蓝图编辑器后「变量批量编辑器」Tab 自动出现"), bAutoOpened);
				}

				if (!bAutoOpened && Test)
				{
					// 即使既存布局里没有它，也应该可以从「窗口」菜单/代码里手动打开。
					Test->TestTrue(TEXT("「变量批量编辑器」Tab 可以被手动打开"),
						TabManager->TryInvokeTab(BatchTabId).IsValid());
				}
			}

			if (AssetEditorSubsystem && Blueprint.IsValid())
			{
				AssetEditorSubsystem->CloseAllEditorsForAsset(Blueprint.Get());
			}

			if (UBlueprint* BlueprintObject = Blueprint.Get())
			{
				if (UPackage* Package = BlueprintObject->GetOutermost())
				{
					Package->SetDirtyFlag(false);
				}
			}

			return true;
		}

	private:
		TWeakObjectPtr<UBlueprint> Blueprint;
		FAutomationTestBase* Test;
	};
}

bool FVariableBatchTabSpawnTest::RunTest(const FString& Parameters)
{
	// 无渲染器（-nullrhi）时 Slate 无法创建浮动的编辑器窗口，会触发 GenericWindow 致命错误；
	// 这种环境下跳过，留待交互式编辑器里执行。
	if (GUsingNullRHI)
	{
		AddInfo(TEXT("当前进程使用 NullRHI（无渲染器），跳过窗口相关的 Tab 测试。"));
		return true;
	}

	if (!TestNotNull(TEXT("GEditor 存在"), GEditor))
	{
		return false;
	}

	UPackage* Package = CreatePackage(TEXT("/Temp/BP_VariableBatchTabTest"));
	if (!TestNotNull(TEXT("临时包创建成功"), Package))
	{
		return false;
	}

	UBlueprint* Blueprint = FKismetEditorUtilities::CreateBlueprint(
		AActor::StaticClass(),
		Package,
		TEXT("BP_VariableBatchTabTest"),
		BPTYPE_Normal,
		UBlueprint::StaticClass(),
		UBlueprintGeneratedClass::StaticClass());

	if (!TestNotNull(TEXT("临时蓝图创建成功"), Blueprint))
	{
		return false;
	}

	UAssetEditorSubsystem* AssetEditorSubsystem = GEditor->GetEditorSubsystem<UAssetEditorSubsystem>();
	if (!TestNotNull(TEXT("资产编辑器子系统存在"), AssetEditorSubsystem))
	{
		return false;
	}

	AssetEditorSubsystem->OpenEditorForAsset(Blueprint);

	if (!TestNotNull(TEXT("蓝图编辑器实例已创建"),
		AssetEditorSubsystem->FindEditorForAsset(Blueprint, false)))
	{
		return false;
	}

	// 布局扩展让 Tab 默认出现在 MyBlueprint 旁边；对已保存布局，模块还有一次 FTSTicker 兜底打开（约 30 帧）。
	ADD_LATENT_AUTOMATION_COMMAND(FVariableBatchWaitFramesCommand(120));
	ADD_LATENT_AUTOMATION_COMMAND(FVariableBatchCheckTabCommand(Blueprint, this));

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
