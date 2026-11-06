#pragma once

#include "CoreMinimal.h"
#include "FVariableBatchTypes.h"

class UBlueprint;

/**
 * 收集蓝图内所有可批量修改类型的变量（成员变量 + 函数局部变量），并统计引用节点数。
 *
 * 引用统计采用「刷新时一次性遍历所有图」的方式，而不是对每个变量调用
 * FBlueprintEditorUtils::GetNodesForVariable（后者为 O(变量数 × 节点数)）。
 */
class FVariableBatchCollector
{
public:
	explicit FVariableBatchCollector(UBlueprint* InBlueprint);

	/** 收集变量行；蓝图无效时返回 false 并清空结果。 */
	bool Collect(TArray<TSharedPtr<FVariableBatchEntry>>& OutEntries) const;

private:
	UBlueprint* Blueprint = nullptr;
};
