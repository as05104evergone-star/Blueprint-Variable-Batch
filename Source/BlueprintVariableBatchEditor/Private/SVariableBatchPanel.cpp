#include "SVariableBatchPanel.h"

#include "BlueprintEditor.h"
#include "BlueprintEditorModule.h"
#include "BlueprintEditorTabs.h"
#include "EdGraphSchema_K2.h"
#include "Engine/Blueprint.h"
#include "FVariableBatchApplier.h"
#include "FVariableBatchCollector.h"
#include "Framework/Docking/TabManager.h"
#include "SPinTypeSelector.h"
#include "Styling/AppStyle.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Input/SComboBox.h"
#include "Widgets/Input/SSearchBox.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Views/SHeaderRow.h"
#include "Widgets/Views/SListView.h"
#include "Widgets/Views/STableRow.h"

#define LOCTEXT_NAMESPACE "SVariableBatchPanel"

namespace
{
	const FName ColumnName_VariableName(TEXT("VariableName"));
	const FName ColumnName_VariableType(TEXT("VariableType"));
	const FName ColumnName_Scope(TEXT("Scope"));
	const FName ColumnName_Refs(TEXT("Refs"));
	const FName ColumnName_Hint(TEXT("Hint"));

	/** 表格行：把 FVariableBatchEntry 映射为多列显示。 */
	class SVariableBatchRow : public SMultiColumnTableRow<TSharedPtr<FVariableBatchEntry>>
	{
	public:
		SLATE_BEGIN_ARGS(SVariableBatchRow) {}
			SLATE_ARGUMENT(TSharedPtr<FVariableBatchEntry>, Entry)
		SLATE_END_ARGS()

		void Construct(const FArguments& InArgs, const TSharedRef<STableViewBase>& InOwnerTable)
		{
			Entry = InArgs._Entry;
			SMultiColumnTableRow<TSharedPtr<FVariableBatchEntry>>::Construct(FSuperRowType::FArguments(), InOwnerTable);
		}

		virtual TSharedRef<SWidget> GenerateWidgetForColumn(const FName& InColumnName) override
		{
			if (!Entry.IsValid())
			{
				return SNew(STextBlock).Text(FText::GetEmpty());
			}

			const FSlateFontInfo Font = FAppStyle::GetFontStyle("PropertyWindow.NormalFont");
			const FSlateColor TextColor = Entry->bIsDelegate
				? FSlateColor::UseSubduedForeground()
				: FSlateColor::UseForeground();

			const FText RowToolTip = FText::FromString(FString::Printf(
				TEXT("%s\n当前类型：%s\n范围：%s"),
				*Entry->DisplayName,
				*UEdGraphSchema_K2::TypeToText(Entry->Type).ToString(),
				*Entry->ScopeLabel));

			if (InColumnName == ColumnName_VariableName)
			{
				return SNew(STextBlock)
					.Font(Font)
					.ColorAndOpacity(TextColor)
					.Text(FText::FromString(Entry->DisplayName))
					.ToolTipText(RowToolTip);
			}

			if (InColumnName == ColumnName_VariableType)
			{
				return SNew(STextBlock)
					.Font(Font)
					.ColorAndOpacity(TextColor)
					.Text(UEdGraphSchema_K2::TypeToText(Entry->Type))
					.ToolTipText(RowToolTip);
			}

			if (InColumnName == ColumnName_Scope)
			{
				return SNew(STextBlock)
					.Font(Font)
					.ColorAndOpacity(TextColor)
					.Text(FText::FromString(Entry->ScopeLabel))
					.ToolTipText(RowToolTip);
			}

			if (InColumnName == ColumnName_Refs)
			{
				FString RefsText = FString::FromInt(Entry->GetTotalRefCount());
				if (Entry->ChildRefCount > 0)
				{
					RefsText += FString::Printf(TEXT("（子 %d）"), Entry->ChildRefCount);
				}

				return SNew(STextBlock)
					.Font(Font)
					.ColorAndOpacity(Entry->GetTotalRefCount() > 0
						? FSlateColor(FLinearColor(0.95f, 0.72f, 0.15f))
						: TextColor)
					.Text(FText::FromString(RefsText))
					.ToolTipText(LOCTEXT("RefsToolTip", "引用该变量的节点数（本蓝图 + 已加载子蓝图）。"));
			}

			// Hint 列
			FText HintText;
			if (Entry->bIsDelegate)
			{
				HintText = LOCTEXT("HintDelegate", "类型不可更改");
			}
			else if (Entry->bIsLocal && Entry->bHasDefaultValue)
			{
				HintText = LOCTEXT("HintLocalDefault", "默认值会重置");
			}

			return SNew(STextBlock)
				.Font(Font)
				.ColorAndOpacity(FSlateColor::UseSubduedForeground())
				.Text(HintText)
				.ToolTipText(LOCTEXT("HintToolTip", "事件分发器的类型不可更改；局部变量的默认值在改类型后会被清空。"));
		}

	private:
		TSharedPtr<FVariableBatchEntry> Entry;
	};
}

void SVariableBatchPanel::Construct(const FArguments& InArgs)
{
	BlueprintEditorPtr = InArgs._BlueprintEditor;

	PendingType.PinCategory = UEdGraphSchema_K2::PC_Boolean;
	PendingType.PinSubCategory = NAME_None;

	TArray<TSharedPtr<IPinTypeSelectorFilter>> PinTypeFilters;
	if (TSharedPtr<FBlueprintEditor> BlueprintEditor = BlueprintEditorPtr.Pin())
	{
		BlueprintEditor->GetPinTypeSelectorFilters(PinTypeFilters);
	}

	ChildSlot
	[
		SNew(SVerticalBox)

		// 工具条
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(4.f, 4.f, 4.f, 2.f)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			.FillWidth(1.f)
			.VAlign(VAlign_Center)
			[
				SAssignNew(SearchBox, SSearchBox)
				.HintText(LOCTEXT("FilterHint", "按变量名过滤…"))
				.OnTextChanged(this, &SVariableBatchPanel::OnFilterTextChanged)
			]
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.Padding(4.f, 0.f, 0.f, 0.f)
			[
				SNew(SButton)
				.OnClicked(this, &SVariableBatchPanel::OnSelectAllClicked)
				.ToolTipText(LOCTEXT("SelectAllTip", "选中当前列表中的全部可改类型变量"))
				[
					SNew(STextBlock).Text(LOCTEXT("SelectAll", "全选"))
				]
			]
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.Padding(4.f, 0.f, 0.f, 0.f)
			[
				SNew(SButton)
				.OnClicked(this, &SVariableBatchPanel::OnSelectNoneClicked)
				.ToolTipText(LOCTEXT("SelectNoneTip", "取消所有选中"))
				[
					SNew(STextBlock).Text(LOCTEXT("SelectNone", "全不选"))
				]
			]
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.Padding(4.f, 0.f, 0.f, 0.f)
			[
				SNew(SButton)
				.OnClicked(this, &SVariableBatchPanel::OnInvertClicked)
				.ToolTipText(LOCTEXT("InvertTip", "反选当前列表中的可改类型变量"))
				[
					SNew(STextBlock).Text(LOCTEXT("Invert", "反选"))
				]
			]
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.Padding(4.f, 0.f, 0.f, 0.f)
			[
				SNew(SButton)
				.OnClicked(this, &SVariableBatchPanel::OnRefreshClicked)
				.ToolTipText(LOCTEXT("RefreshTip", "重新扫描蓝图中的变量"))
				[
					SNew(STextBlock).Text(LOCTEXT("Refresh", "刷新"))
				]
			]
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.Padding(8.f, 0.f, 0.f, 0.f)
			.VAlign(VAlign_Center)
			[
				SNew(STextBlock)
				.Text(this, &SVariableBatchPanel::GetCountsText)
				.ColorAndOpacity(FSlateColor::UseSubduedForeground())
			]
		]

		// 变量表格
		+ SVerticalBox::Slot()
		.FillHeight(1.f)
		.Padding(4.f, 2.f)
		[
			SNew(SBorder)
			.BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder"))
			.Padding(1.f)
			[
				SAssignNew(ListView, SListView<TSharedPtr<FVariableBatchEntry>>)
				.ListItemsSource(&FilteredEntries)
				.SelectionMode(ESelectionMode::Multi)
				.OnGenerateRow(this, &SVariableBatchPanel::OnGenerateRow)
				.OnSelectionChanged(this, &SVariableBatchPanel::OnSelectionChanged)
				.HeaderRow(
					SNew(SHeaderRow)
					+ SHeaderRow::Column(ColumnName_VariableName)
						.DefaultLabel(LOCTEXT("ColumnVariableName", "变量"))
						.FillWidth(0.30f)
					+ SHeaderRow::Column(ColumnName_VariableType)
						.DefaultLabel(LOCTEXT("ColumnVariableType", "当前类型"))
						.FillWidth(0.30f)
					+ SHeaderRow::Column(ColumnName_Scope)
						.DefaultLabel(LOCTEXT("ColumnScope", "范围"))
						.FillWidth(0.20f)
					+ SHeaderRow::Column(ColumnName_Refs)
						.DefaultLabel(LOCTEXT("ColumnRefs", "引用"))
						.FillWidth(0.09f)
					+ SHeaderRow::Column(ColumnName_Hint)
						.DefaultLabel(LOCTEXT("ColumnHint", "提示"))
						.FillWidth(0.23f)
				)
			]
		]

		// 应用区
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(4.f, 2.f, 4.f, 4.f)
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot()
			.AutoHeight()
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.VAlign(VAlign_Center)
				[
					SNew(SCheckBox)
					.Style(FAppStyle::Get(), "RadioButton")
					.IsChecked(this, &SVariableBatchPanel::GetSetModeCheckState)
					.OnCheckStateChanged(this, &SVariableBatchPanel::OnSetModeCheckStateChanged)
					[
						SNew(STextBlock).Text(LOCTEXT("ModeSet", "统一设置为"))
					]
				]
				+ SHorizontalBox::Slot()
				.FillWidth(1.f)
				.Padding(8.f, 0.f, 0.f, 0.f)
				.VAlign(VAlign_Center)
				[
					SAssignNew(TypeSelector, SPinTypeSelector, FGetPinTypeTree::CreateUObject(
						GetDefault<UEdGraphSchema_K2>(), &UEdGraphSchema_K2::GetVariableTypeTree))
					.TargetPinType(this, &SVariableBatchPanel::GetPendingType)
					.OnPinTypeChanged(this, &SVariableBatchPanel::OnPendingTypeChanged)
					.Schema(GetDefault<UEdGraphSchema_K2>())
					.TypeTreeFilter(ETypeTreeFilter::None)
					.Font(FAppStyle::GetFontStyle("PropertyWindow.NormalFont"))
					.CustomFilters(PinTypeFilters)
					.IsEnabled(TAttribute<bool>::CreateLambda([this]() { return !bCopyTypeMode; }))
				]
			]
			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(0.f, 4.f, 0.f, 0.f)
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.VAlign(VAlign_Center)
				[
					SNew(SCheckBox)
					.Style(FAppStyle::Get(), "RadioButton")
					.IsChecked(this, &SVariableBatchPanel::GetCopyModeCheckState)
					.OnCheckStateChanged(this, &SVariableBatchPanel::OnCopyModeCheckStateChanged)
					[
						SNew(STextBlock).Text(LOCTEXT("ModeCopy", "取类型自选中变量"))
					]
				]
				+ SHorizontalBox::Slot()
				.FillWidth(1.f)
				.Padding(8.f, 0.f, 0.f, 0.f)
				.VAlign(VAlign_Center)
				[
					SAssignNew(TemplateCombo, SComboBox<TSharedPtr<FVariableBatchEntry>>)
					.OptionsSource(&TemplateOptions)
					.OnGenerateWidget(this, &SVariableBatchPanel::OnGenerateTemplateComboItem)
					.OnSelectionChanged(this, &SVariableBatchPanel::OnTemplateSelectionChanged)
					.IsEnabled(TAttribute<bool>::CreateLambda([this]() { return bCopyTypeMode; }))
					[
						SNew(STextBlock).Text(this, &SVariableBatchPanel::GetTemplateComboText)
					]
				]
			]
			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(0.f, 8.f, 0.f, 0.f)
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot()
				.FillWidth(1.f)
				.VAlign(VAlign_Center)
				[
					SNew(STextBlock)
					.Text(this, &SVariableBatchPanel::GetStatusText)
					.ColorAndOpacity(FSlateColor::UseSubduedForeground())
					.AutoWrapText(true)
				]
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.Padding(8.f, 0.f, 0.f, 0.f)
				[
					SNew(SButton)
					.OnClicked(this, &SVariableBatchPanel::OnApplyClicked)
					.IsEnabled(this, &SVariableBatchPanel::CanApply)
					.ToolTipText(this, &SVariableBatchPanel::GetApplyTooltip)
					[
						SNew(STextBlock).Text(this, &SVariableBatchPanel::GetApplyButtonText)
					]
				]
			]
		]
	];

	RefreshEntries();

	if (TSharedPtr<FBlueprintEditor> BlueprintEditor = BlueprintEditorPtr.Pin())
	{
		EditorRefreshHandle = BlueprintEditor->OnRefresh().AddSP(this, &SVariableBatchPanel::HandleEditorRefresh);
	}

	if (UBlueprint* Blueprint = GetBlueprint())
	{
		BlueprintCompiledHandle = Blueprint->OnCompiled().AddSP(this, &SVariableBatchPanel::HandleBlueprintCompiled);
	}
}

SVariableBatchPanel::~SVariableBatchPanel()
{
	if (TSharedPtr<FBlueprintEditor> BlueprintEditor = BlueprintEditorPtr.Pin())
	{
		if (EditorRefreshHandle.IsValid())
		{
			BlueprintEditor->OnRefresh().Remove(EditorRefreshHandle);
		}
	}

	if (UBlueprint* Blueprint = GetBlueprint())
	{
		if (BlueprintCompiledHandle.IsValid())
		{
			Blueprint->OnCompiled().Remove(BlueprintCompiledHandle);
		}
	}
}

UBlueprint* SVariableBatchPanel::GetBlueprint() const
{
	if (TSharedPtr<FBlueprintEditor> BlueprintEditor = BlueprintEditorPtr.Pin())
	{
		return BlueprintEditor->GetBlueprintObj();
	}

	return nullptr;
}

void SVariableBatchPanel::RefreshEntries()
{
	AllEntries.Reset();
	FVariableBatchCollector(GetBlueprint()).Collect(AllEntries);
	RebuildFilteredEntries();

	UBlueprint* Blueprint = GetBlueprint();
	UE_LOG(LogTemp, Display, TEXT("[BlueprintVariableBatch] 面板刷新：蓝图 %s，共收集到 %d 个变量。"),
		Blueprint ? *Blueprint->GetName() : TEXT("<无效>"), AllEntries.Num());
}

void SVariableBatchPanel::HandleTabActivated(TSharedRef<SDockTab> Tab, ETabActivationCause Cause)
{
	if (Cause == ETabActivationCause::SetDirectly || Cause == ETabActivationCause::UserClickedOnTab)
	{
		RefreshEntries();
	}
}

void SVariableBatchPanel::HandleEditorRefresh()
{
	if (!bApplying)
	{
		RefreshEntries();
	}
}

void SVariableBatchPanel::HandleBlueprintCompiled(UBlueprint* CompiledBlueprint)
{
	if (!bApplying && CompiledBlueprint == GetBlueprint())
	{
		RefreshEntries();
	}
}

TSharedRef<ITableRow> SVariableBatchPanel::OnGenerateRow(
	TSharedPtr<FVariableBatchEntry> Item, const TSharedRef<STableViewBase>& OwnerTable)
{
	return SNew(SVariableBatchRow, OwnerTable)
		.Entry(Item);
}

void SVariableBatchPanel::OnSelectionChanged(TSharedPtr<FVariableBatchEntry> Item, ESelectInfo::Type SelectInfo)
{
	if (bUpdatingSelection || !ListView.IsValid())
	{
		return;
	}

	// 事件分发器不可改类型：批量选择（如 Ctrl+A）时把它们剔除出去。
	bool bStrippedAny = false;
	bUpdatingSelection = true;
	for (const TSharedPtr<FVariableBatchEntry>& Entry : FilteredEntries)
	{
		if (Entry.IsValid() && Entry->bIsDelegate && ListView->IsItemSelected(Entry))
		{
			ListView->SetItemSelection(Entry, false, ESelectInfo::Direct);
			bStrippedAny = true;
		}
	}
	bUpdatingSelection = false;

	UpdateTemplateOptions();
}

void SVariableBatchPanel::RestoreSelection()
{
	if (!ListView.IsValid())
	{
		return;
	}

	bUpdatingSelection = true;
	for (const TSharedPtr<FVariableBatchEntry>& Entry : FilteredEntries)
	{
		if (Entry.IsValid() && SelectedKeys.Contains(Entry->GetKey()))
		{
			ListView->SetItemSelection(Entry, true, ESelectInfo::Direct);
		}
	}
	bUpdatingSelection = false;

	UpdateTemplateOptions();
}

TArray<TSharedPtr<FVariableBatchEntry>> SVariableBatchPanel::GetSelectedEntries() const
{
	TArray<TSharedPtr<FVariableBatchEntry>> Selected;
	if (!ListView.IsValid())
	{
		return Selected;
	}

	for (const TSharedPtr<FVariableBatchEntry>& Entry : FilteredEntries)
	{
		if (Entry.IsValid() && ListView->IsItemSelected(Entry))
		{
			Selected.Add(Entry);
		}
	}

	return Selected;
}

void SVariableBatchPanel::OnFilterTextChanged(const FText& InFilterText)
{
	FilterText = InFilterText.ToString();
	RebuildFilteredEntries();
}

void SVariableBatchPanel::RebuildFilteredEntries()
{
	// 记录当前选中，重建后恢复。
	SelectedKeys.Reset();
	if (ListView.IsValid())
	{
		for (const TSharedPtr<FVariableBatchEntry>& Entry : FilteredEntries)
		{
			if (Entry.IsValid() && ListView->IsItemSelected(Entry))
			{
				SelectedKeys.Add(Entry->GetKey());
			}
		}
		ListView->ClearSelection();
	}

	FilteredEntries.Reset();
	const FString Filter = FilterText.TrimStartAndEnd();

	for (const TSharedPtr<FVariableBatchEntry>& Entry : AllEntries)
	{
		if (!Entry.IsValid())
		{
			continue;
		}

		if (!Filter.IsEmpty()
			&& !Entry->DisplayName.Contains(Filter, ESearchCase::IgnoreCase)
			&& !Entry->Name.ToString().Contains(Filter, ESearchCase::IgnoreCase))
		{
			continue;
		}

		FilteredEntries.Add(Entry);
	}

	if (ListView.IsValid())
	{
		ListView->RequestListRefresh();
	}

	RestoreSelection();
}

FReply SVariableBatchPanel::OnSelectAllClicked()
{
	if (ListView.IsValid())
	{
		bUpdatingSelection = true;
		for (const TSharedPtr<FVariableBatchEntry>& Entry : FilteredEntries)
		{
			if (Entry.IsValid() && !Entry->bIsDelegate)
			{
				ListView->SetItemSelection(Entry, true, ESelectInfo::Direct);
			}
		}
		bUpdatingSelection = false;
	}

	UpdateTemplateOptions();
	return FReply::Handled();
}

FReply SVariableBatchPanel::OnSelectNoneClicked()
{
	if (ListView.IsValid())
	{
		ListView->ClearSelection();
	}

	UpdateTemplateOptions();
	return FReply::Handled();
}

FReply SVariableBatchPanel::OnInvertClicked()
{
	if (ListView.IsValid())
	{
		bUpdatingSelection = true;
		for (const TSharedPtr<FVariableBatchEntry>& Entry : FilteredEntries)
		{
			if (Entry.IsValid() && !Entry->bIsDelegate)
			{
				ListView->SetItemSelection(Entry, !ListView->IsItemSelected(Entry), ESelectInfo::Direct);
			}
		}
		bUpdatingSelection = false;
	}

	UpdateTemplateOptions();
	return FReply::Handled();
}

FReply SVariableBatchPanel::OnRefreshClicked()
{
	RefreshEntries();
	StatusText = LOCTEXT("Refreshed", "已重新扫描蓝图变量。");
	return FReply::Handled();
}

FText SVariableBatchPanel::GetCountsText() const
{
	const int32 NumSelected = GetSelectedEntries().Num();
	return FText::FromString(FString::Printf(TEXT("已选 %d / 共 %d"), NumSelected, FilteredEntries.Num()));
}

FReply SVariableBatchPanel::OnApplyClicked()
{
	UBlueprint* Blueprint = GetBlueprint();
	if (!Blueprint)
	{
		StatusText = LOCTEXT("NoBlueprintError", "没有可编辑的蓝图。");
		return FReply::Handled();
	}

	if (bApplying)
	{
		return FReply::Handled();
	}

	const FEdGraphPinType TargetType = ResolveTargetType();

	FText Error;
	if (!FVariableBatchApplier::ValidateTargetType(TargetType, Error))
	{
		StatusText = Error;
		return FReply::Handled();
	}

	const TArray<TSharedPtr<FVariableBatchEntry>> Selected = GetSelectedEntries();
	const FVariableBatchPlan Plan = FVariableBatchApplier::BuildPlan(Blueprint, Selected, TargetType);
	if (!Plan.IsValid())
	{
		StatusText = Plan.FailureReason;
		return FReply::Handled();
	}

	if (!FVariableBatchApplier::ConfirmPlan(Blueprint, Plan))
	{
		StatusText = LOCTEXT("ApplyCancelled", "已取消，未修改任何变量。");
		return FReply::Handled();
	}

	bApplying = true;
	const FVariableBatchApplyResult Result = FVariableBatchApplier::ApplyPlan(Blueprint, Plan);
	bApplying = false;

	SelectedKeys.Reset();
	if (ListView.IsValid())
	{
		ListView->ClearSelection();
	}

	RefreshEntries();

	StatusText = Result.StatusText;
	UE_LOG(LogTemp, Display, TEXT("[BlueprintVariableBatch] 批量应用完成：%s"), *Result.StatusText.ToString());

	if (Result.HasCompileErrors())
	{
		// 打开编译器结果标签，方便查看逐条错误。
		if (TSharedPtr<FBlueprintEditor> BlueprintEditor = BlueprintEditorPtr.Pin())
		{
			BlueprintEditor->GetTabManager()->TryInvokeTab(FTabId(FBlueprintEditorTabs::CompilerResultsID));
		}
	}

	return FReply::Handled();
}

FEdGraphPinType SVariableBatchPanel::ResolveTargetType() const
{
	if (bCopyTypeMode && TemplateSelection.IsValid())
	{
		return TemplateSelection->Type;
	}

	return PendingType;
}

ECheckBoxState SVariableBatchPanel::GetSetModeCheckState() const
{
	return bCopyTypeMode ? ECheckBoxState::Unchecked : ECheckBoxState::Checked;
}

ECheckBoxState SVariableBatchPanel::GetCopyModeCheckState() const
{
	return bCopyTypeMode ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
}

void SVariableBatchPanel::OnSetModeCheckStateChanged(ECheckBoxState NewState)
{
	if (NewState == ECheckBoxState::Checked)
	{
		bCopyTypeMode = false;
		StatusText = FText::GetEmpty();
	}
}

void SVariableBatchPanel::OnCopyModeCheckStateChanged(ECheckBoxState NewState)
{
	if (NewState == ECheckBoxState::Checked)
	{
		bCopyTypeMode = true;
		StatusText = FText::GetEmpty();
		UpdateTemplateOptions();
	}
}

void SVariableBatchPanel::OnPendingTypeChanged(const FEdGraphPinType& NewType)
{
	PendingType = NewType;
}

void SVariableBatchPanel::UpdateTemplateOptions()
{
	TemplateOptions.Reset();

	if (ListView.IsValid())
	{
		for (const TSharedPtr<FVariableBatchEntry>& Entry : FilteredEntries)
		{
			if (Entry.IsValid() && !Entry->bIsDelegate && ListView->IsItemSelected(Entry))
			{
				TemplateOptions.Add(Entry);
			}
		}
	}

	// 尽量保持原来的模板选择。
	TSharedPtr<FVariableBatchEntry> PreviousSelection = TemplateSelection;
	TemplateSelection.Reset();

	if (TemplateOptions.Num() > 0)
	{
		if (PreviousSelection.IsValid())
		{
			for (const TSharedPtr<FVariableBatchEntry>& Entry : TemplateOptions)
			{
				if (Entry.IsValid() && Entry->GetKey() == PreviousSelection->GetKey())
				{
					TemplateSelection = Entry;
					break;
				}
			}
		}

		if (!TemplateSelection.IsValid())
		{
			TemplateSelection = TemplateOptions[0];
		}
	}

	if (TemplateCombo.IsValid())
	{
		TemplateCombo->RefreshOptions();
		TemplateCombo->SetSelectedItem(TemplateSelection);
	}
}

TSharedRef<SWidget> SVariableBatchPanel::OnGenerateTemplateComboItem(TSharedPtr<FVariableBatchEntry> Entry) const
{
	const FString Label = Entry.IsValid()
		? FString::Printf(TEXT("%s（%s）"), *Entry->DisplayName, *UEdGraphSchema_K2::TypeToText(Entry->Type).ToString())
		: FString();

	return SNew(STextBlock).Text(FText::FromString(Label));
}

void SVariableBatchPanel::OnTemplateSelectionChanged(TSharedPtr<FVariableBatchEntry> Entry, ESelectInfo::Type SelectInfo)
{
	TemplateSelection = Entry;
}

FText SVariableBatchPanel::GetTemplateComboText() const
{
	if (TemplateSelection.IsValid())
	{
		return FText::FromString(FString::Printf(TEXT("%s（%s）"),
			*TemplateSelection->DisplayName,
			*UEdGraphSchema_K2::TypeToText(TemplateSelection->Type).ToString()));
	}

	return LOCTEXT("PickTemplate", "选择作为类型来源的变量…");
}

FText SVariableBatchPanel::GetApplyButtonText() const
{
	const int32 NumSelected = GetSelectedEntries().Num();
	return FText::FromString(FString::Printf(TEXT("应用到选中 %d 个变量"), NumSelected));
}

bool SVariableBatchPanel::CanApply() const
{
	if (bApplying)
	{
		return false;
	}

	UBlueprint* Blueprint = GetBlueprint();
	if (!Blueprint)
	{
		return false;
	}

	FText Error;
	if (!FVariableBatchApplier::IsBlueprintEditable(Blueprint, Error))
	{
		return false;
	}

	return GetSelectedEntries().Num() > 0;
}

FText SVariableBatchPanel::GetApplyTooltip() const
{
	if (bApplying)
	{
		return LOCTEXT("ApplyingToolTip", "正在应用修改…");
	}

	UBlueprint* Blueprint = GetBlueprint();
	if (!Blueprint)
	{
		return LOCTEXT("NoBlueprintToolTip", "没有可编辑的蓝图。");
	}

	FText Error;
	if (!FVariableBatchApplier::IsBlueprintEditable(Blueprint, Error))
	{
		return Error;
	}

	if (GetSelectedEntries().Num() == 0)
	{
		return LOCTEXT("NoSelectionToolTip", "请先在表格中选择要修改的变量。");
	}

	const FString TypeText = UEdGraphSchema_K2::TypeToText(ResolveTargetType()).ToString();
	return FText::FromString(FString::Printf(TEXT("把选中的变量统一修改为：%s"), *TypeText));
}

FText SVariableBatchPanel::GetStatusText() const
{
	return StatusText;
}

#undef LOCTEXT_NAMESPACE
