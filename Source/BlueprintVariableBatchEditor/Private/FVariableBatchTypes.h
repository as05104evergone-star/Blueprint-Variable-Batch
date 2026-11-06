#pragma once

#include "CoreMinimal.h"
#include "EdGraph/EdGraphPin.h"

class UFunction;
class UK2Node_FunctionEntry;

/** 面板中的一行：一个可批量修改类型的变量。 */
struct FVariableBatchEntry
{
	/** 变量名 */
	FName Name;

	/** 当前类型 */
	FEdGraphPinType Type;

	/** 是否为函数/宏局部变量 */
	bool bIsLocal = false;

	/** 是否为事件分发器（Delegate），其类型不可更改 */
	bool bIsDelegate = false;

	/** 局部变量所属的函数签名 */
	TWeakObjectPtr<UFunction> Scope;

	/** 局部变量所在的函数入口节点 */
	TWeakObjectPtr<UK2Node_FunctionEntry> FunctionEntry;

	/** 范围显示文本：成员 / 函数名 */
	FString ScopeLabel;

	/** 显示用变量名（优先使用 FriendlyName） */
	FString DisplayName;

	/** 本蓝图内引用该变量的节点数 */
	int32 RefCount = 0;

	/** 已加载子蓝图内引用该变量的节点数 */
	int32 ChildRefCount = 0;

	/** 局部变量是否带有非空默认值（改类型后会被清空） */
	bool bHasDefaultValue = false;

	int32 GetTotalRefCount() const { return RefCount + ChildRefCount; }

	/** 行唯一键（同名但作用域不同的变量可共存） */
	FString GetKey() const
	{
		return FString::Printf(TEXT("%s|%s"), *ScopeLabel, *Name.ToString());
	}
};

/** 一次批量操作的风险摘要，用于向用户确认。 */
struct FVariableBatchRiskSummary
{
	int32 NumMembers = 0;
	int32 NumLocals = 0;
	int32 NumReferenced = 0;
	int32 NumRefNodes = 0;
	int32 NumChildRefNodes = 0;
	int32 NumLoseReplication = 0;
	int32 NumBoolRenamed = 0;
	int32 NumLocalDefaultsReset = 0;
};

/** 批量修改的执行计划（预检结果 + 待修改变量 + 目标类型）。 */
struct FVariableBatchPlan
{
	TArray<TSharedPtr<FVariableBatchEntry>> ToChange;

	FEdGraphPinType TargetType;

	FVariableBatchRiskSummary Risks;

	/** 因是事件分发器而被跳过的变量数 */
	int32 NumSkippedDelegates = 0;

	/** 无法执行的原因；为空表示计划有效 */
	FText FailureReason;

	bool IsValid() const { return FailureReason.IsEmpty(); }
};

/** 批量修改的执行结果。 */
struct FVariableBatchApplyResult
{
	int32 NumChanged = 0;
	int32 NumReconstructedNodes = 0;
	int32 NumCompileErrors = 0;
	TArray<FString> CompileErrorLines;
	TArray<FString> ChangedNames;

	FText StatusText;

	bool HasCompileErrors() const { return NumCompileErrors > 0; }
};
