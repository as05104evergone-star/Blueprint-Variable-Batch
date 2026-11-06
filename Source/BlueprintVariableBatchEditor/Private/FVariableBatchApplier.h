#pragma once

#include "CoreMinimal.h"
#include "FVariableBatchTypes.h"

class UBlueprint;

/**
 * 批量修改蓝图变量类型：预检、确认、执行。
 *
 * 执行流程刻意不走 FBlueprintEditorUtils::ChangeMemberVariableType / ChangeLocalVariableType，
 * 因为这两个函数会为每个变量各弹一次确认框、各开一次引用搜索窗、各做一次结构修改与事务。
 * 这里改为：单个事务 → 复刻引擎的字段处理 → 全部写完后只做一次结构修改 → 批量重建节点 → 编译一次。
 */
class FVariableBatchApplier
{
public:
	/** 蓝图当前是否可被修改（非编译中、非只读、非加载中）。 */
	static bool IsBlueprintEditable(UBlueprint* Blueprint, FText& OutError);

	/** 校验目标引脚类型能否作为变量类型使用。 */
	static bool ValidateTargetType(const FEdGraphPinType& PinType, FText& OutError);

	/** 构建执行计划：过滤掉类型相同与不可改类型的变量，并统计风险项。 */
	static FVariableBatchPlan BuildPlan(
		UBlueprint* Blueprint,
		const TArray<TSharedPtr<FVariableBatchEntry>>& Candidates,
		const FEdGraphPinType& RequestedType);

	/** 弹出一次总确认框，返回用户是否确认执行。 */
	static bool ConfirmPlan(UBlueprint* Blueprint, const FVariableBatchPlan& Plan);

	/** 执行整批修改。 */
	static FVariableBatchApplyResult ApplyPlan(UBlueprint* Blueprint, const FVariableBatchPlan& Plan);

	/** 构建「批量修改变量类别」的执行计划：只保留类别确实会变化的成员变量。 */
	static FVariableBatchCategoryPlan BuildCategoryPlan(
		UBlueprint* Blueprint,
		const TArray<TSharedPtr<FVariableBatchEntry>>& Candidates,
		const FText& RequestedCategory);

	/** 弹出类别修改的确认框。 */
	static bool ConfirmCategoryPlan(UBlueprint* Blueprint, const FVariableBatchCategoryPlan& Plan);

	/** 执行整批类别修改（单事务；只标记结构修改，不重建节点、不额外编译）。 */
	static FVariableBatchApplyResult ApplyCategoryPlan(UBlueprint* Blueprint, const FVariableBatchCategoryPlan& Plan);
};
