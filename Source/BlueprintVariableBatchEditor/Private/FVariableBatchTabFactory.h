#pragma once

#include "CoreMinimal.h"
#include "WorkflowOrientedApp/WorkflowTabFactory.h"

class FBlueprintEditor;
class SVariableBatchPanel;

/**
 * 蓝图编辑器内的「变量批量编辑器」Tab 工厂。
 */
class FVariableBatchTabFactory : public FWorkflowTabFactory
{
public:
	explicit FVariableBatchTabFactory(TSharedPtr<FBlueprintEditor> InBlueprintEditor);

	//~ Begin FWorkflowTabFactory
	virtual TSharedRef<SWidget> CreateTabBody(const FWorkflowTabSpawnInfo& Info) const override;
	virtual TSharedRef<SDockTab> SpawnTab(const FWorkflowTabSpawnInfo& Info) const override;
	virtual FText GetTabToolTipText(const FWorkflowTabSpawnInfo& Info) const override;
	//~ End FWorkflowTabFactory

private:
	TWeakPtr<FBlueprintEditor> BlueprintEditorPtr;

	/** CreateTabBody 创建的实例，用于在 SpawnTab 中挂接激活刷新回调。 */
	mutable TWeakPtr<SVariableBatchPanel> PanelPtr;
};
