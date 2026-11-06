#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"
#include "Containers/Ticker.h"

class FBlueprintEditor;
class FWorkflowAllowedTabSet;
class FVariableBatchTabFactory;
class FLayoutExtender;

/**
 * 「蓝图变量批量编辑器」插件模块。
 *
 * 通过 FBlueprintEditorModule 的 OnRegisterTabsForEditor / OnRegisterLayoutExtensions
 * 扩展点向所有蓝图编辑器注册一个可停靠面板，面板内可多选变量并批量修改类型。
 */
class FBlueprintVariableBatchEditorModule : public IModuleInterface
{
public:
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;

	/** 面板 Tab 的标识名 */
	static FName GetTabId();

private:
	void HandleRegisterTabs(FWorkflowAllowedTabSet& TabSet, FName ModeName, TSharedPtr<FBlueprintEditor> BlueprintEditor);

	void HandleRegisterLayoutExtensions(FLayoutExtender& LayoutExtender);

	/** 对已存在布局的编辑器做一次性兜底打开（新布局已由 LayoutExtender 处理）。 */
	void ScheduleAutoOpen(const TSharedPtr<FBlueprintEditor>& BlueprintEditor);

private:
	FDelegateHandle RegisterTabsHandle;
	FDelegateHandle RegisterLayoutHandle;

	TArray<FTSTicker::FDelegateHandle> TickerHandles;
};
