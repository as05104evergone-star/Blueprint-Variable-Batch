#pragma once

#include "CoreMinimal.h"
#include "EdGraph/EdGraph.h"
#include "Engine/Blueprint.h"
#include "K2Node.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "UObject/UObjectIterator.h"

/**
 * 引擎里 FBlueprintEditorUtils::GetLoadedChildBlueprints / GetNodesForVariable
 * 在 5.7 中是 protected，插件无法直接调用；这里按引擎实现复刻一份公开版本。
 */
namespace VariableBatchUtils
{
	/** 收集所有已加载、且以 InBlueprint 为父类（或实现了该蓝图接口）的子蓝图。 */
	inline void GetLoadedChildBlueprints(UBlueprint* InBlueprint, TArray<UBlueprint*>& OutBlueprints)
	{
		OutBlueprints.Reset();
		if (!InBlueprint)
		{
			return;
		}

		for (TObjectIterator<UBlueprint> BlueprintIt; BlueprintIt; ++BlueprintIt)
		{
			UBlueprint* ChildBP = *BlueprintIt;
			if (!ChildBP || ChildBP == InBlueprint || !ChildBP->ParentClass)
			{
				continue;
			}

			TArray<UBlueprint*> ParentBPArray;
			UBlueprint::GetBlueprintHierarchyFromClass(ChildBP->ParentClass, ParentBPArray);

			// 蓝图接口里声明的变量同样会被实现者引用到
			for (const FBPInterfaceDescription& InterfaceDesc : ChildBP->ImplementedInterfaces)
			{
				if (UBlueprint* InterfaceBP = UBlueprint::GetBlueprintFromClass(InterfaceDesc.Interface))
				{
					ParentBPArray.Add(InterfaceBP);
				}
			}

			if (ParentBPArray.Contains(InBlueprint))
			{
				OutBlueprints.Add(ChildBP);
			}
		}
	}

	/** 收集蓝图内引用了指定变量（可限定作用域）的所有节点，等价于引擎的 GetNodesForVariable。 */
	inline TArray<UK2Node*> GetNodesForVariable(const FName& InVarName, const UBlueprint* InBlueprint, const UStruct* InScope = nullptr)
	{
		TArray<UK2Node*> Result;
		if (!InBlueprint || InVarName.IsNone())
		{
			return Result;
		}

		TArray<UEdGraph*> Graphs;
		InBlueprint->GetAllGraphs(Graphs);
		for (UEdGraph* Graph : Graphs)
		{
			if (!Graph)
			{
				continue;
			}

			for (UEdGraphNode* GraphNode : Graph->Nodes)
			{
				if (UK2Node* K2Node = Cast<UK2Node>(GraphNode))
				{
					if (K2Node->ReferencesVariable(InVarName, InScope))
					{
						Result.Add(K2Node);
					}
				}
			}
		}

		return Result;
	}
}
