#pragma once

#include "CoreMinimal.h"
#include "EdGraph/EdGraphPin.h"
#include "FVariableBatchTypes.h"
#include "Widgets/Docking/SDockTab.h"
#include "Widgets/Input/SComboBox.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/Views/SListView.h"

class FBlueprintEditor;
class SEditableTextBox;
class SHeaderRow;
class SPinTypeSelector;
class SSearchBox;
class UBlueprint;

/**
 * 「变量批量编辑器」面板：表格列出蓝图中的成员变量与函数局部变量，
 * 支持加选、过滤，并把选中变量统一改为同一个类型（或取自某个变量）。
 */
class SVariableBatchPanel : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SVariableBatchPanel) {}
		SLATE_ARGUMENT(TSharedPtr<FBlueprintEditor>, BlueprintEditor)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);
	virtual ~SVariableBatchPanel() override;

	/** 重新收集变量并刷新列表（尽量保留选中项与过滤文本）。 */
	void RefreshEntries();

	/** Tab 被激活时的刷新入口，由 FVariableBatchTabFactory 挂钩。 */
	void HandleTabActivated(TSharedRef<SDockTab> Tab, ETabActivationCause Cause);

private:
	UBlueprint* GetBlueprint() const;

	//~ 列表
	TSharedRef<ITableRow> OnGenerateRow(TSharedPtr<FVariableBatchEntry> Item, const TSharedRef<STableViewBase>& OwnerTable);
	void OnSelectionChanged(TSharedPtr<FVariableBatchEntry> Item, ESelectInfo::Type SelectInfo);
	void RestoreSelection();

	/** 当前被选中的变量（含事件分发器，由 Applier 负责跳过）。 */
	TArray<TSharedPtr<FVariableBatchEntry>> GetSelectedEntries() const;

	//~ 工具栏
	void OnFilterTextChanged(const FText& InFilterText);
	FReply OnSelectAllClicked();
	FReply OnSelectNoneClicked();
	FReply OnInvertClicked();
	FReply OnRefreshClicked();
	void RebuildFilteredEntries();
	FText GetCountsText() const;

	//~ 应用区
	FReply OnApplyClicked();
	ECheckBoxState GetSetModeCheckState() const;
	ECheckBoxState GetCopyModeCheckState() const;
	void OnSetModeCheckStateChanged(ECheckBoxState NewState);
	void OnCopyModeCheckStateChanged(ECheckBoxState NewState);
	FEdGraphPinType GetPendingType() const { return PendingType; }
	void OnPendingTypeChanged(const FEdGraphPinType& NewType);
	TSharedRef<SWidget> OnGenerateTemplateComboItem(TSharedPtr<FVariableBatchEntry> Entry) const;
	void OnTemplateSelectionChanged(TSharedPtr<FVariableBatchEntry> Entry, ESelectInfo::Type SelectInfo);
	FText GetTemplateComboText() const;
	FText GetApplyButtonText() const;
	bool CanApply() const;
	FText GetApplyTooltip() const;
	FText GetStatusText() const;
	void UpdateTemplateOptions();
	FEdGraphPinType ResolveTargetType() const;

	//~ 类别区
	FReply OnApplyCategoryClicked();
	bool CanApplyCategory() const;
	FText GetApplyCategoryButtonText() const;
	FText GetApplyCategoryTooltip() const;
	FText GetCategoryInputText() const;
	void OnCategoryInputChanged(const FText& NewText);
	TSharedRef<SWidget> OnGenerateCategoryComboItem(TSharedPtr<FString> Item) const;
	void OnCategoryComboSelectionChanged(TSharedPtr<FString> Item, ESelectInfo::Type SelectInfo);
	FText GetCategoryComboText() const;
	void UpdateCategoryOptions();
	FText GetRequestedCategory() const;

	//~ 刷新事件
	void HandleEditorRefresh();
	void HandleBlueprintCompiled(UBlueprint* CompiledBlueprint);

private:
	TWeakPtr<FBlueprintEditor> BlueprintEditorPtr;

	TArray<TSharedPtr<FVariableBatchEntry>> AllEntries;
	TArray<TSharedPtr<FVariableBatchEntry>> FilteredEntries;

	/** 刷新前记录的选中行 key，刷新后据此恢复选中。 */
	TSet<FString> SelectedKeys;

	TSharedPtr<SListView<TSharedPtr<FVariableBatchEntry>>> ListView;
	TSharedPtr<SSearchBox> SearchBox;
	TSharedPtr<SPinTypeSelector> TypeSelector;
	TSharedPtr<SComboBox<TSharedPtr<FVariableBatchEntry>>> TemplateCombo;
	TArray<TSharedPtr<FVariableBatchEntry>> TemplateOptions;
	TSharedPtr<FVariableBatchEntry> TemplateSelection;

	FString FilterText;

	TSharedPtr<SEditableTextBox> CategoryInput;
	TSharedPtr<SComboBox<TSharedPtr<FString>>> CategoryCombo;
	TArray<TSharedPtr<FString>> CategoryOptions;
	FText CategoryInputText;
	FEdGraphPinType PendingType;
	bool bCopyTypeMode = false;
	bool bUpdatingSelection = false;
	bool bApplying = false;

	FText StatusText;

	FDelegateHandle EditorRefreshHandle;
	FDelegateHandle BlueprintCompiledHandle;
};
