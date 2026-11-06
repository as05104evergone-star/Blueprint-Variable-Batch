#include "FVariableBatchTabFactory.h"

#include "BlueprintEditor.h"
#include "BlueprintVariableBatchEditor.h"
#include "SVariableBatchPanel.h"
#include "Styling/AppStyle.h"
#include "Widgets/Docking/SDockTab.h"

#define LOCTEXT_NAMESPACE "FVariableBatchTabFactory"

FVariableBatchTabFactory::FVariableBatchTabFactory(TSharedPtr<FBlueprintEditor> InBlueprintEditor)
	: FWorkflowTabFactory(FBlueprintVariableBatchEditorModule::GetTabId(), InBlueprintEditor)
	, BlueprintEditorPtr(InBlueprintEditor)
{
	TabLabel = LOCTEXT("TabLabel", "变量批量编辑器");
	TabIcon = FSlateIcon(FAppStyle::GetAppStyleSetName(), "ClassIcon.BlueprintCore");
	bIsSingleton = true;

	ViewMenuDescription = LOCTEXT("ViewMenuDescription", "变量批量编辑器");
	ViewMenuTooltip = LOCTEXT("ViewMenuTooltip", "批量选择变量并统一修改类型（成员变量与函数局部变量）。");
}

TSharedRef<SWidget> FVariableBatchTabFactory::CreateTabBody(const FWorkflowTabSpawnInfo& Info) const
{
	TSharedRef<SVariableBatchPanel> Panel = SNew(SVariableBatchPanel)
		.BlueprintEditor(BlueprintEditorPtr.Pin());

	PanelPtr = Panel;
	return Panel;
}

TSharedRef<SDockTab> FVariableBatchTabFactory::SpawnTab(const FWorkflowTabSpawnInfo& Info) const
{
	TSharedRef<SDockTab> Tab = FWorkflowTabFactory::SpawnTab(Info);

	if (TSharedPtr<SVariableBatchPanel> Panel = PanelPtr.Pin())
	{
		Tab->SetOnTabActivated(SDockTab::FOnTabActivatedCallback::CreateSP(
			Panel.ToSharedRef(), &SVariableBatchPanel::HandleTabActivated));
	}

	return Tab;
}

FText FVariableBatchTabFactory::GetTabToolTipText(const FWorkflowTabSpawnInfo& Info) const
{
	return LOCTEXT("TabToolTip", "多选蓝图变量并批量修改它们的类型。\n支持成员变量与函数/宏局部变量，整批一次撤销。");
}

#undef LOCTEXT_NAMESPACE
