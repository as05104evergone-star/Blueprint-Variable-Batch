#include "FVariableBatchApplier.h"

#include "Dialogs/Dialogs.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphNode.h"
#include "EdGraphSchema_K2.h"
#include "Engine/Blueprint.h"
#include "GameFramework/Actor.h"
#include "HAL/FileManager.h"
#include "K2Node_FunctionEntry.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "FVariableBatchUtils.h"
#include "Kismet2/CompilerResultsLog.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Logging/TokenizedMessage.h"
#include "Misc/PackageName.h"
#include "ScopedTransaction.h"
#include "UObject/Package.h"
#include "UObject/UObjectGlobals.h"

#define LOCTEXT_NAMESPACE "FVariableBatchApplier"

namespace
{
	bool IsDelegateCategory(const FName& Category)
	{
		return Category == UEdGraphSchema_K2::PC_Delegate
			|| Category == UEdGraphSchema_K2::PC_MCDelegate;
	}

	bool ValidateCategoryObject(const FName& Category, const UObject* SubObject, FText& OutError)
	{
		if (Category == UEdGraphSchema_K2::PC_Struct)
		{
			if (!Cast<UScriptStruct>(SubObject))
			{
				OutError = LOCTEXT("InvalidStruct", "目标类型无效：结构体类型缺少有效的结构体定义。");
				return false;
			}
		}
		else if (Category == UEdGraphSchema_K2::PC_Object
			|| Category == UEdGraphSchema_K2::PC_Class
			|| Category == UEdGraphSchema_K2::PC_SoftClass
			|| Category == UEdGraphSchema_K2::PC_SoftObject
			|| Category == UEdGraphSchema_K2::PC_Interface)
		{
			if (!Cast<UClass>(SubObject))
			{
				OutError = LOCTEXT("InvalidClass", "目标类型无效：对象或接口类型必须指定有效的类。");
				return false;
			}
		}
		else if (Category == UEdGraphSchema_K2::PC_Enum)
		{
			if (!Cast<UEnum>(SubObject))
			{
				OutError = LOCTEXT("InvalidEnum", "目标类型无效：枚举类型缺少有效的枚举定义。");
				return false;
			}
		}
		else if (IsDelegateCategory(Category))
		{
			if (SubObject && !Cast<UFunction>(SubObject))
			{
				OutError = LOCTEXT("InvalidDelegate", "目标类型无效：事件分发器必须指定有效的签名函数。");
				return false;
			}
		}

		return true;
	}

	/** 复刻引擎 ChangeMemberVariableType / ChangeLocalVariableType 的类型写入与附带字段处理。*/
	void ApplyTypeToVariable(FBPVariableDescription& Variable, const FEdGraphPinType& NewPinType, bool bIsLocal)
	{
		// 对象/接口类型需要同步「模板上不可编辑」标记（AActor 类型的变量在关卡实例面板里不可编辑）。
		if (NewPinType.PinCategory == UEdGraphSchema_K2::PC_Object
			|| NewPinType.PinCategory == UEdGraphSchema_K2::PC_Interface)
		{
			if (const UClass* ClassObject = Cast<UClass>(NewPinType.PinSubCategoryObject.Get()))
			{
				if (ClassObject->IsChildOf(AActor::StaticClass()))
				{
					Variable.PropertyFlags |= CPF_DisableEditOnTemplate;
				}
				else
				{
					Variable.PropertyFlags &= ~CPF_DisableEditOnTemplate;
				}
			}
			else
			{
				Variable.PropertyFlags &= ~CPF_DisableEditOnTemplate;
			}
		}

		// Bool 与非 Bool 互转时，按引擎行为刷新显示名（重新分词并添加空格）。
		const bool bBecameBoolean = NewPinType.PinCategory == UEdGraphSchema_K2::PC_Boolean;
		const bool bWasBoolean = Variable.VarType.PinCategory == UEdGraphSchema_K2::PC_Boolean;
		if (bBecameBoolean != bWasBoolean)
		{
			Variable.FriendlyName = FName::NameToDisplayString(Variable.VarName.ToString(), bBecameBoolean);
		}

		Variable.VarType = NewPinType;

		// 容器类型不支持网络复制，切换。Set/Map 时与引擎一致地清除复制标记。
		if (!bIsLocal && (Variable.VarType.IsSet() || Variable.VarType.IsMap()))
		{
			if ((Variable.PropertyFlags & (CPF_Net | CPF_RepNotify)) != 0
				|| !Variable.RepNotifyFunc.IsNone()
				|| Variable.ReplicationCondition != COND_None)
			{
				Variable.PropertyFlags &= ~(CPF_Net | CPF_RepNotify);
				Variable.RepNotifyFunc = NAME_None;
				Variable.ReplicationCondition = COND_None;
			}
		}

		// 局部变量的默认值不随类型保留（引擎既有行为），这里保持一致。
		if (bIsLocal)
		{
			Variable.DefaultValue.Empty();
		}
	}

	const FBPVariableDescription* FindMemberVariable(UBlueprint* Blueprint, const FName& VariableName)
	{
		const int32 VariableIndex = FBlueprintEditorUtils::FindNewVariableIndex(Blueprint, VariableName);
		return Blueprint->NewVariables.IsValidIndex(VariableIndex) ? &Blueprint->NewVariables[VariableIndex] : nullptr;
	}

	/** 从蓝图里读取某个变量「此刻」的类型；读取失败返回 false。*/
	bool GetCurrentVariableType(UBlueprint* Blueprint, const FVariableBatchEntry& Entry, FEdGraphPinType& OutType)
	{
		if (Entry.bIsLocal)
		{
			const UFunction* Scope = Entry.Scope.Get();
			if (!Scope)
			{
				return false;
			}

			UK2Node_FunctionEntry* FunctionEntry = nullptr;
			if (const FBPVariableDescription* Variable =
				FBlueprintEditorUtils::FindLocalVariable(Blueprint, Scope, Entry.Name, &FunctionEntry))
			{
				OutType = Variable->VarType;
				return true;
			}

			return false;
		}

		if (const FBPVariableDescription* Variable = FindMemberVariable(Blueprint, Entry.Name))
		{
			OutType = Variable->VarType;
			return true;
		}

		return false;
	}
}

bool FVariableBatchApplier::IsBlueprintEditable(UBlueprint* Blueprint, FText& OutError)
{
	if (!Blueprint)
	{
		OutError = LOCTEXT("NoBlueprint", "没有可编辑的蓝图。");
		return false;
	}

	if (Blueprint->bBeingCompiled || Blueprint->bQueuedForCompilation)
	{
		OutError = LOCTEXT("BlueprintCompiling", "蓝图正在编译中，请等编译结束后再试。");
		return false;
	}

	if (Blueprint->bIsRegeneratingOnLoad)
	{
		OutError = LOCTEXT("BlueprintRegenerating", "蓝图正在加载/重建中，请稍后再试。");
		return false;
	}

	if (const UPackage* Package = Blueprint->GetOutermost())
	{
		const FString Filename = FPackageName::LongPackageNameToFilename(
			Package->GetName(), FPackageName::GetAssetPackageExtension());

		if (IFileManager::Get().IsReadOnly(*Filename))
		{
			OutError = LOCTEXT("BlueprintReadOnly", "蓝图资产文件是只读的，无法修改。");
			return false;
		}
	}

	return true;
}

bool FVariableBatchApplier::ValidateTargetType(const FEdGraphPinType& PinType, FText& OutError)
{
	if (PinType.PinCategory.IsNone())
	{
		OutError = LOCTEXT("NoPinCategory", "目标类型无效：请先选择要应用的变量类型。");
		return false;
	}

	if (!ValidateCategoryObject(PinType.PinCategory, PinType.PinSubCategoryObject.Get(), OutError))
	{
		return false;
	}

	if (PinType.ContainerType == EPinContainerType::Map)
	{
		const FEdGraphTerminalType& ValueType = PinType.PinValueType;
		if (ValueType.TerminalCategory.IsNone())
		{
			OutError = LOCTEXT("NoMapValueType", "目标类型无效：映射（Map）的值类型不能为空。");
			return false;
		}

		if (!ValidateCategoryObject(ValueType.TerminalCategory, ValueType.TerminalSubCategoryObject.Get(), OutError))
		{
			return false;
		}
	}

	return true;
}

FVariableBatchPlan FVariableBatchApplier::BuildPlan(
	UBlueprint* Blueprint,
	const TArray<TSharedPtr<FVariableBatchEntry>>& Candidates,
	const FEdGraphPinType& RequestedType)
{
	FVariableBatchPlan Plan;
	Plan.TargetType = RequestedType;

	FText Error;
	if (!IsBlueprintEditable(Blueprint, Error))
	{
		Plan.FailureReason = Error;
		return Plan;
	}

	if (!ValidateTargetType(RequestedType, Error))
	{
		Plan.FailureReason = Error;
		return Plan;
	}

	for (const TSharedPtr<FVariableBatchEntry>& Candidate : Candidates)
	{
		if (!Candidate.IsValid())
		{
			continue;
		}

		// 事件分发器的类型不可更改，直接跳过。
		if (Candidate->bIsDelegate)
		{
			Plan.NumSkippedDelegates++;
			continue;
		}

		// 以蓝图里的当前类型为准（面板数据可能已过期），已经一致就跳过。
		FEdGraphPinType CurrentType;
		if (GetCurrentVariableType(Blueprint, *Candidate, CurrentType) && CurrentType == RequestedType)
		{
			continue;
		}

		Plan.ToChange.Add(Candidate);
	}

	if (Plan.ToChange.Num() == 0)
	{
		Plan.FailureReason = LOCTEXT("NoChangeNeeded", "选中的变量当前类型已经与目标类型一致，没有需要修改的变量。");
		return Plan;
	}

	FVariableBatchRiskSummary& Risks = Plan.Risks;
	const bool bTargetIsBoolean = RequestedType.PinCategory == UEdGraphSchema_K2::PC_Boolean;
	const bool bTargetIsContainer = RequestedType.IsSet() || RequestedType.IsMap();

	for (const TSharedPtr<FVariableBatchEntry>& Entry : Plan.ToChange)
	{
		if (Entry->bIsLocal)
		{
			Risks.NumLocals++;
			if (Entry->bHasDefaultValue)
			{
				Risks.NumLocalDefaultsReset++;
			}
		}
		else
		{
			Risks.NumMembers++;

			if (bTargetIsContainer)
			{
				if (const FBPVariableDescription* Variable = FindMemberVariable(Blueprint, Entry->Name))
				{
					const bool bReplicated = ((Variable->PropertyFlags & (CPF_Net | CPF_RepNotify)) != 0)
						|| !Variable->RepNotifyFunc.IsNone()
						|| Variable->ReplicationCondition != COND_None;
					if (bReplicated)
					{
						Risks.NumLoseReplication++;
					}
				}
			}
		}

		if (Entry->GetTotalRefCount() > 0)
		{
			Risks.NumReferenced++;
		}
		Risks.NumRefNodes += Entry->RefCount;
		Risks.NumChildRefNodes += Entry->ChildRefCount;

		FEdGraphPinType PreviousType;
		const FEdGraphPinType& OldType = GetCurrentVariableType(Blueprint, *Entry, PreviousType)
			? PreviousType
			: Entry->Type;
		const bool bWasBoolean = OldType.PinCategory == UEdGraphSchema_K2::PC_Boolean;
		if (bWasBoolean != bTargetIsBoolean)
		{
			Risks.NumBoolRenamed++;
		}
	}

	return Plan;
}

bool FVariableBatchApplier::ConfirmPlan(UBlueprint* Blueprint, const FVariableBatchPlan& Plan)
{
	if (!Blueprint || !Plan.IsValid() || Plan.ToChange.Num() == 0)
	{
		return false;
	}

	const FString TypeText = UEdGraphSchema_K2::TypeToText(Plan.TargetType).ToString();

	FString Message = FString::Printf(
		TEXT("将修改 %d 个变量（成员 %d 个、函数局部 %d 个）。\n目标类型：%s\n"),
		Plan.ToChange.Num(), Plan.Risks.NumMembers, Plan.Risks.NumLocals, *TypeText);

	if (Plan.Risks.NumReferenced > 0)
	{
		Message += FString::Printf(
			TEXT("\n• 其中 %d 个变量被 %d 个节点引用（%d 处来自已加载的子蓝图）。\n  这些节点会被重建，与新类型不兼容的连线会断开。\n"),
			Plan.Risks.NumReferenced, Plan.Risks.NumRefNodes, Plan.Risks.NumChildRefNodes);
	}

	if (Plan.Risks.NumLoseReplication > 0)
	{
		Message += FString::Printf(
			TEXT("\n• %d 个成员变量会失去网络复制标记（复制变量不支持 Set/Map 容器）。\n"),
			Plan.Risks.NumLoseReplication);
	}

	if (Plan.Risks.NumBoolRenamed > 0)
	{
		Message += FString::Printf(
			TEXT("\n。%d 个变量的显示名会随 Bool 类型变化而更新。\n"),
			Plan.Risks.NumBoolRenamed);
	}

	if (Plan.Risks.NumLocalDefaultsReset > 0)
	{
		Message += FString::Printf(
			TEXT("\n• %d 个局部变量的默认值会被清空（引擎既有行为，不随类型保留）。\n"),
			Plan.Risks.NumLocalDefaultsReset);
	}

	if (Plan.NumSkippedDelegates > 0)
	{
		Message += FString::Printf(
			TEXT("\n• 已跳过 %d 个事件分发器（其类型不可更改）。\n"),
			Plan.NumSkippedDelegates);
	}

	Message += TEXT("\n整批修改可以用一次 Ctrl+Z 撤销。\n是否继续？");

	return FMessageDialog::Open(
		EAppMsgType::OkCancel,
		FText::FromString(Message),
		LOCTEXT("ConfirmTitle", "批量改变量类型")) == EAppReturnType::Ok;
}

FVariableBatchApplyResult FVariableBatchApplier::ApplyPlan(UBlueprint* Blueprint, const FVariableBatchPlan& Plan)
{
	FVariableBatchApplyResult Result;
	if (!Blueprint || !Plan.IsValid() || Plan.ToChange.Num() == 0)
	{
		Result.StatusText = LOCTEXT("NothingToApply", "没有需要修改的变量。");
		return Result;
	}

	const UEdGraphSchema_K2* K2Schema = GetDefault<UEdGraphSchema_K2>();

	TArray<UK2Node*> NodesToReconstruct;
	TSet<UK2Node*> UniqueNodesToReconstruct;
	TArray<UBlueprint*> ChildBlueprintsToMark;
	TSet<UBlueprint*> UniqueChildBlueprints;

	TArray<UBlueprint*> LoadedChildBlueprints;
	VariableBatchUtils::GetLoadedChildBlueprints(Blueprint, LoadedChildBlueprints);

	{
		// 整批修改共用同一个事务，撤销时一次回退全部变量。
		const FScopedTransaction Transaction(LOCTEXT("BatchChangeVariableType", "批量改变量类型"));
		Blueprint->Modify();

		for (const TSharedPtr<FVariableBatchEntry>& Entry : Plan.ToChange)
		{
			if (!Entry.IsValid())
			{
				continue;
			}

			if (!Entry->bIsLocal)
			{
				const int32 VariableIndex = FBlueprintEditorUtils::FindNewVariableIndex(Blueprint, Entry->Name);
				if (!Blueprint->NewVariables.IsValidIndex(VariableIndex))
				{
					continue;
				}

				FBPVariableDescription& Variable = Blueprint->NewVariables[VariableIndex];
				if (Variable.VarType == Plan.TargetType)
				{
					continue;
				}

				ApplyTypeToVariable(Variable, Plan.TargetType, /*bIsLocal=*/false);
				Result.NumChanged++;
				Result.ChangedNames.Add(Entry->DisplayName);

				for (UK2Node* Node : VariableBatchUtils::GetNodesForVariable(Entry->Name, Blueprint))
				{
					if (Node && !UniqueNodesToReconstruct.Contains(Node))
					{
						UniqueNodesToReconstruct.Add(Node);
						NodesToReconstruct.Add(Node);
					}
				}

				for (UBlueprint* ChildBlueprint : LoadedChildBlueprints)
				{
					if (!ChildBlueprint)
					{
						continue;
					}

					for (UK2Node* Node : VariableBatchUtils::GetNodesForVariable(Entry->Name, ChildBlueprint))
					{
						if (!Node)
						{
							continue;
						}

						if (!UniqueNodesToReconstruct.Contains(Node))
						{
							UniqueNodesToReconstruct.Add(Node);
							NodesToReconstruct.Add(Node);
						}

						if (!UniqueChildBlueprints.Contains(ChildBlueprint))
						{
							UniqueChildBlueprints.Add(ChildBlueprint);
							ChildBlueprintsToMark.Add(ChildBlueprint);
						}
					}
				}
			}
			else
			{
				UFunction* Scope = Entry->Scope.Get();
				if (!Scope)
				{
					continue;
				}

				UK2Node_FunctionEntry* FunctionEntry = nullptr;
				FBPVariableDescription* Variable = FBlueprintEditorUtils::FindLocalVariable(Blueprint, Scope, Entry->Name, &FunctionEntry);
				if (!Variable || Variable->VarType == Plan.TargetType)
				{
					continue;
				}

				if (FunctionEntry)
				{
					FunctionEntry->Modify();
				}

				ApplyTypeToVariable(*Variable, Plan.TargetType, /*bIsLocal=*/true);
				Result.NumChanged++;
				Result.ChangedNames.Add(Entry->DisplayName);
			}
		}

		// 所有变量都写完后只做一次结构修改，避免逐变量触发编辑器刷新。
		if (Result.NumChanged > 0)
		{
			FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
			for (UBlueprint* ChildBlueprint : ChildBlueprintsToMark)
			{
				FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(ChildBlueprint);
			}
		}
	}

	// 逐条修改时收集的是成员变量在子蓝图中的引用；局部变量只存在于本蓝图，需另行收集。
	for (const TSharedPtr<FVariableBatchEntry>& Entry : Plan.ToChange)
	{
		if (!Entry.IsValid() || !Entry->bIsLocal)
		{
			continue;
		}

		UFunction* Scope = Entry->Scope.Get();
		if (!Scope)
		{
			continue;
		}

		for (UK2Node* Node : VariableBatchUtils::GetNodesForVariable(Entry->Name, Blueprint, Scope))
		{
			if (Node && !UniqueNodesToReconstruct.Contains(Node))
			{
				UniqueNodesToReconstruct.Add(Node);
				NodesToReconstruct.Add(Node);
			}
		}
	}

	// 重建受影响的节点，让引脚与新类型对齐（断开的连线由引擎处理）。
	for (UK2Node* Node : NodesToReconstruct)
	{
		if (Node)
		{
			K2Schema->ReconstructNode(*Node, /*bIsBatchRequest=*/true);
		}
	}
	Result.NumReconstructedNodes = NodesToReconstruct.Num();

	// 全部修改完成后编译一次。
	FCompilerResultsLog CompileLog;
	FKismetEditorUtilities::CompileBlueprint(Blueprint, EBlueprintCompileOptions::None, &CompileLog);

	Result.NumCompileErrors = CompileLog.NumErrors;
	for (const TSharedRef<FTokenizedMessage>& CompileMessage : CompileLog.Messages)
	{
		if (CompileMessage->GetSeverity() == EMessageSeverity::Error)
		{
			Result.CompileErrorLines.Add(CompileMessage->ToText().ToString());
		}
	}

	if (Result.HasCompileErrors())
	{
		Result.StatusText = FText::FromString(FString::Printf(
			TEXT("已修改 %d 个变量，重建 %d 个节点；编译发现 %d 个错误（未回滚），请查看「编译器结果」标签。"),
			Result.NumChanged, Result.NumReconstructedNodes, Result.NumCompileErrors));
	}
	else
	{
		Result.StatusText = FText::FromString(FString::Printf(
			TEXT("成功修改 %d 个变量，重建 %d 个节点，编译通过。"),
			Result.NumChanged, Result.NumReconstructedNodes));
	}

	return Result;
}

#undef LOCTEXT_NAMESPACE
