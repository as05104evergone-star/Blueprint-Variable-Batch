#include "FVariableBatchCollector.h"

#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphNode.h"
#include "EdGraphSchema_K2.h"
#include "Engine/Blueprint.h"
#include "K2Node_FunctionEntry.h"
#include "K2Node_Variable.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "FVariableBatchUtils.h"

namespace
{
	/** 引用计数：ByName 忽略作用域（对齐引擎成员变量语义），ByScope 精确到函数作用域。*/
	struct FReferenceCounts
	{
		TMap<FName, int32> ByName;
		TMap<FName, TMap<FName, int32>> ByScope;
	};

	/** 一次性遍历一张蓝图的所有图，统计变量引用节点。*/
	void CountVariableNodes(UBlueprint* InBlueprint, FReferenceCounts& OutCounts)
	{
		if (!InBlueprint)
		{
			return;
		}

		TArray<UEdGraph*> Graphs;
		InBlueprint->GetAllGraphs(Graphs);

		for (UEdGraph* Graph : Graphs)
		{
			if (!Graph)
			{
				continue;
			}

			for (int32 NodeIndex = 0; NodeIndex < Graph->Nodes.Num(); ++NodeIndex)
			{
				UEdGraphNode* Node = Graph->Nodes[NodeIndex];
				const UK2Node_Variable* VariableNode = Cast<UK2Node_Variable>(Node);
				if (!VariableNode)
				{
					continue;
				}

				const FName VariableName = VariableNode->GetVarName();
				if (VariableName.IsNone())
				{
					continue;
				}

				OutCounts.ByName.FindOrAdd(VariableName)++;

				// 局部变量的作用域名就是所属函数名（见 UK2Node_Variable::ReferencesVariable）。
				const FString ScopeName = VariableNode->VariableReference.GetMemberScopeName();
				if (!ScopeName.IsEmpty())
				{
					OutCounts.ByScope.FindOrAdd(FName(*ScopeName)).FindOrAdd(VariableName)++;
				}
			}
		}
	}

	FString MakeEntryDisplayName(const FBPVariableDescription& Variable)
	{
		if (!Variable.FriendlyName.IsEmpty())
		{
			return Variable.FriendlyName;
		}

		return FName::NameToDisplayString(
			Variable.VarName.ToString(),
			Variable.VarType.PinCategory == UEdGraphSchema_K2::PC_Boolean);
	}
}

FVariableBatchCollector::FVariableBatchCollector(UBlueprint* InBlueprint)
	: Blueprint(InBlueprint)
{
}

bool FVariableBatchCollector::Collect(TArray<TSharedPtr<FVariableBatchEntry>>& OutEntries) const
{
	OutEntries.Reset();

	if (!Blueprint)
	{
		return false;
	}

	FReferenceCounts SelfCounts;
	CountVariableNodes(Blueprint, SelfCounts);

	FReferenceCounts ChildCounts;
	TArray<UBlueprint*> ChildBlueprints;
	VariableBatchUtils::GetLoadedChildBlueprints(Blueprint, ChildBlueprints);
	for (UBlueprint* ChildBlueprint : ChildBlueprints)
	{
		CountVariableNodes(ChildBlueprint, ChildCounts);
	}

	// 成员变量
	for (int32 VariableIndex = 0; VariableIndex < Blueprint->NewVariables.Num(); ++VariableIndex)
	{
		const FBPVariableDescription& Variable = Blueprint->NewVariables[VariableIndex];

		// 只收录确实登记在 NewVariables 中的变量（排。SCS 组件等特殊情况）。
		if (FBlueprintEditorUtils::FindNewVariableIndex(Blueprint, Variable.VarName) != VariableIndex)
		{
			continue;
		}

		TSharedPtr<FVariableBatchEntry> Entry = MakeShared<FVariableBatchEntry>();
		Entry->Name = Variable.VarName;
		Entry->Type = Variable.VarType;
		Entry->bIsLocal = false;
		Entry->bIsDelegate = Variable.VarType.PinCategory == UEdGraphSchema_K2::PC_Delegate;
		Entry->ScopeLabel = TEXT("成员");
		Entry->DisplayName = MakeEntryDisplayName(Variable);
		Entry->RefCount = SelfCounts.ByName.FindRef(Variable.VarName);
		Entry->ChildRefCount = ChildCounts.ByName.FindRef(Variable.VarName);
		Entry->bHasDefaultValue = !Variable.DefaultValue.IsEmpty();

		OutEntries.Add(Entry);
	}

	// 函数局部变。
	for (UEdGraph* FunctionGraph : Blueprint->FunctionGraphs)
	{
		if (!FunctionGraph || !FBlueprintEditorUtils::DoesSupportLocalVariables(FunctionGraph))
		{
			continue;
		}

		UK2Node_FunctionEntry* FunctionEntry = nullptr;
		TArray<UK2Node_FunctionEntry*> FunctionEntryNodes;
		FunctionGraph->GetNodesOfClass<UK2Node_FunctionEntry>(FunctionEntryNodes);
		if (FunctionEntryNodes.Num() > 0)
		{
			FunctionEntry = FunctionEntryNodes[0];
		}

		if (!FunctionEntry)
		{
			continue;
		}

		UFunction* SignatureFunction = FunctionEntry->FindSignatureFunction();
		if (!SignatureFunction)
		{
			continue;
		}

		// 优先。schema 的局部变量接口，未实现时退回读取函数入口节点上的变量数组。
		TArray<FBPVariableDescription> LocalVariables;
		const UEdGraphSchema* Schema = FunctionGraph->GetSchema();
		const bool bGotFromSchema = Schema && Schema->GetLocalVariables(FunctionGraph, LocalVariables);
		if (!bGotFromSchema)
		{
			LocalVariables = FunctionEntry->LocalVariables;
		}

		const FName ScopeName = SignatureFunction->GetFName();
		const TMap<FName, int32>* SelfScopeCounts = SelfCounts.ByScope.Find(ScopeName);
		const TMap<FName, int32>* ChildScopeCounts = ChildCounts.ByScope.Find(ScopeName);

		for (const FBPVariableDescription& Variable : LocalVariables)
		{
			TSharedPtr<FVariableBatchEntry> Entry = MakeShared<FVariableBatchEntry>();
			Entry->Name = Variable.VarName;
			Entry->Type = Variable.VarType;
			Entry->bIsLocal = true;
			Entry->bIsDelegate = Variable.VarType.PinCategory == UEdGraphSchema_K2::PC_Delegate;
			Entry->Scope = SignatureFunction;
			Entry->FunctionEntry = FunctionEntry;
			Entry->ScopeLabel = FunctionGraph->GetName();
			Entry->DisplayName = MakeEntryDisplayName(Variable);
			Entry->bHasDefaultValue = !Variable.DefaultValue.IsEmpty();
			Entry->RefCount = SelfScopeCounts ? SelfScopeCounts->FindRef(Variable.VarName) : 0;
			Entry->ChildRefCount = ChildScopeCounts ? ChildScopeCounts->FindRef(Variable.VarName) : 0;

			OutEntries.Add(Entry);
		}
	}

	return true;
}
