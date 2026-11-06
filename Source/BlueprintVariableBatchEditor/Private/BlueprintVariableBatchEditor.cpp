#include "BlueprintVariableBatchEditor.h"

#include "BlueprintEditor.h"
#include "BlueprintEditorModes.h"
#include "BlueprintEditorModule.h"
#include "BlueprintEditorTabs.h"
#include "Editor.h"
#include "FVariableBatchTabFactory.h"
#include "Framework/Docking/LayoutExtender.h"
#include "Framework/Docking/TabManager.h"
#include "Modules/ModuleManager.h"
#include "Widgets/Docking/SDockTab.h"
#include "WorkflowOrientedApp/WorkflowTabManager.h"

#define LOCTEXT_NAMESPACE "BlueprintVariableBatchEditorModule"

namespace
{
	const FName BlueprintVariableBatchTabName(TEXT("BlueprintVariableBatchEditor"));
}

void FBlueprintVariableBatchEditorModule::StartupModule()
{
	// Kismet 模块的 StartupModule 里断言 GEditor 必须存在（BlueprintEditorModule.cpp:214），
	// 因此本插件用 PostEngineInit 阶段加载；命令行工具（如 cooker）没有 GEditor，直接跳过绑定。
	if (GEditor == nullptr)
	{
		UE_LOG(LogTemp, Display, TEXT("[BlueprintVariableBatch] 当前进程没有编辑器实例，跳过蓝图编辑器扩展绑定。"));
		return;
	}

	FBlueprintEditorModule& BlueprintEditorModule = FModuleManager::LoadModuleChecked<FBlueprintEditorModule>("Kismet");

	RegisterTabsHandle = BlueprintEditorModule.OnRegisterTabsForEditor().AddRaw(
		this, &FBlueprintVariableBatchEditorModule::HandleRegisterTabs);

	RegisterLayoutHandle = BlueprintEditorModule.OnRegisterLayoutExtensions().AddRaw(
		this, &FBlueprintVariableBatchEditorModule::HandleRegisterLayoutExtensions);

	UE_LOG(LogTemp, Display, TEXT("[BlueprintVariableBatch] 模块已启动：已绑定蓝图编辑器 Tab 与布局扩展点。"));
}

void FBlueprintVariableBatchEditorModule::ShutdownModule()
{
	for (const FTSTicker::FDelegateHandle& TickerHandle : TickerHandles)
	{
		FTSTicker::GetCoreTicker().RemoveTicker(TickerHandle);
	}
	TickerHandles.Reset();

	if (FModuleManager::Get().IsModuleLoaded("Kismet"))
	{
		FBlueprintEditorModule& BlueprintEditorModule = FModuleManager::GetModuleChecked<FBlueprintEditorModule>("Kismet");

		if (RegisterTabsHandle.IsValid())
		{
			BlueprintEditorModule.OnRegisterTabsForEditor().Remove(RegisterTabsHandle);
			RegisterTabsHandle.Reset();
		}

		if (RegisterLayoutHandle.IsValid())
		{
			BlueprintEditorModule.OnRegisterLayoutExtensions().Remove(RegisterLayoutHandle);
			RegisterLayoutHandle.Reset();
		}
	}
}

FName FBlueprintVariableBatchEditorModule::GetTabId()
{
	return BlueprintVariableBatchTabName;
}

void FBlueprintVariableBatchEditorModule::HandleRegisterTabs(
	FWorkflowAllowedTabSet& TabSet, FName ModeName, TSharedPtr<FBlueprintEditor> BlueprintEditor)
{
	if (!BlueprintEditor.IsValid())
	{
		return;
	}

	TabSet.RegisterFactory(MakeShared<FVariableBatchTabFactory>(BlueprintEditor));

	UE_LOG(LogTemp, Verbose, TEXT("[BlueprintVariableBatch] 为模式 %s 注册变量批量编辑器 Tab。"), *ModeName.ToString());

	// 新模式被注册时说明编辑器正在建立自己的布局，兜底打开只需排一次（标准图模式每次编辑器只广播一次）。
	if (ModeName == FBlueprintEditorApplicationModes::StandardBlueprintEditorMode)
	{
		ScheduleAutoOpen(BlueprintEditor);
	}
}

void FBlueprintVariableBatchEditorModule::HandleRegisterLayoutExtensions(FLayoutExtender& LayoutExtender)
{
	// 默认停靠在 My Blueprint 面板右侧；已保存的布局不会被改写，由 ScheduleAutoOpen 兜底。
	LayoutExtender.ExtendLayout(
		FBlueprintEditorTabs::MyBlueprintID,
		ELayoutExtensionPosition::After,
		FTabManager::FTab(FTabId(BlueprintVariableBatchTabName), ETabState::OpenedTab));
}

void FBlueprintVariableBatchEditorModule::ScheduleAutoOpen(const TSharedPtr<FBlueprintEditor>& BlueprintEditor)
{
	TWeakPtr<FBlueprintEditor> WeakEditor = BlueprintEditor;
	TSharedRef<int32> Attempts = MakeShared<int32>(0);

	FTSTicker::FDelegateHandle TickerHandle = FTSTicker::GetCoreTicker().AddTicker(
		FTickerDelegate::CreateLambda([WeakEditor, Attempts](float DeltaTime) -> bool
		{
			TSharedPtr<FBlueprintEditor> Editor = WeakEditor.Pin();
			if (!Editor.IsValid())
			{
				return false;
			}

			// 等待编辑器完成布局构建，再尝试打开一次面板。
			if (++(*Attempts) < 30)
			{
				return true;
			}

			Editor->GetTabManager()->TryInvokeTab(FTabId(BlueprintVariableBatchTabName), /*bInvokeAsInactive=*/true);
			return false;
		}));

	TickerHandles.Add(TickerHandle);
}

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FBlueprintVariableBatchEditorModule, BlueprintVariableBatchEditor)
