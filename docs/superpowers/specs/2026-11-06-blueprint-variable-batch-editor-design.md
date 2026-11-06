# 蓝图变量批量编辑器 · 设计文档

- 日期：2026-11-06
- 目标引擎：Unreal Engine 5.7.4（`E:\UE_5.7`，Changelist 51494982，`++UE5+Release-5.7`）
- 插件路径：`E:\UE_Plugins\BlueprintVariableBatch`
- 插件名：`BlueprintVariableBatch`（FriendlyName「蓝图变量批量编辑器」）
- 状态：设计已与用户逐段确认（5 段全部通过）

---

## 1. 背景与问题核实

### 1.1 用户痛点

制作战斗 Demo 时，每个组件蓝图都要创建大量变量。创建本身不麻烦，但**无法批量修改变量类型**：只能一个一个点开变量、在 Details 面板里改类型。

### 1.2 核实结论：这是引擎缺口，不是操作问题

| 证据 | 位置 |
|---|---|
| My Blueprint 使用 `SGraphActionMenu`，构造时无多选参数 | `E:\UE_5.7\Engine\Source\Editor\Kismet\Private\SMyBlueprint.cpp:558`（`SAssignNew(GraphActionMenu, SGraphActionMenu, false)`） |
| `SGraphActionMenu` 内部 `STreeView` 的选中模式写死为单选，头文件无参数/无 setter | `E:\UE_5.7\Engine\Source\Editor\GraphEditor\Private\SGraphActionMenu.cpp:379`（`.SelectionMode(ESelectionMode::Single)`） |
| 即便多选，Details 派发只取第一个：`SelectionAsVar()` → `SelectedActions[0]` | `SMyBlueprint.cpp:2139-2154`，模板 `SelectionAsType` 在 `L2304-2320` |
| 论坛多年请求多选变量，未实现 | https://forums.unrealengine.com/t/feature-request-select-multiple-variables/402926 |

结论：插件无法把现有 My Blueprint 面板改成多选（要 hook 私有 `STreeView`，引擎升级即失效），因此需要**新增一个自己的可停靠面板**。

### 1.3 可复用的引擎 API（已核实存在且导出）

| 用途 | API | 位置 |
|---|---|---|
| 改成员变量类型 | `FBlueprintEditorUtils::ChangeMemberVariableType(UBlueprint*, const FName, const FEdGraphPinType&)` | 声明 `E:\UE_5.7\Engine\Source\Editor\UnrealEd\Public\Kismet2\BlueprintEditorUtils.h:947`；实现 `...\Private\Kismet2\BlueprintEditorUtils.cpp:5124` |
| 改局部变量类型 | `FBlueprintEditorUtils::ChangeLocalVariableType(UBlueprint*, const UStruct* InScope, const FName, const FEdGraphPinType&)` | 头文件 `L1055`；实现 `L5708` |
| 取某变量的引用节点 | `FBlueprintEditorUtils::GetNodesForVariable(const FName&, const UBlueprint*, const UStruct* InScope = nullptr)` | 头文件 `L1850` |
| 找成员变量索引 | `FBlueprintEditorUtils::FindNewVariableIndex(const UBlueprint*, const FName&)` | 头文件（用于判断「是否可改类型」，与引擎 Details 同条件） |
| 标记结构修改 | `FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(UBlueprint*)` | 头文件 |
| 找局部变量 / 局部变量 scope | `FBlueprintEditorUtils::FindLocalVariable(...)`、`UK2Node_FunctionTerminator::FindSignatureFunction()` | 头文件 `L984-1006`；`E:\UE_5.7\Engine\Source\Editor\BlueprintGraph\Classes\K2Node_FunctionTerminator.h:59` |
| 读图的局部变量 | `UEdGraphSchema::GetLocalVariables(UEdGraph*, TArray<FBPVariableDescription>&)`（K2 实现） | `E:\UE_5.7\Engine\Source\Editor\BlueprintGraph\Classes\EdGraphSchema_K2.h` |
| 类型树 | `UEdGraphSchema_K2::GetVariableTypeTree(TArray<TSharedPtr<FPinTypeTreeInfo>>&, ETypeTreeFilter) const` | `EdGraphSchema_K2.h:1082` |
| 原生类型选择器 | `SPinTypeSelector`（与蓝图 Details 同一控件、同一棵树） | `E:\UE_5.7\Engine\Source\Editor\KismetWidgets\Public\SPinTypeSelector.h`；原生用法 `E:\UE_5.7\Engine\Source\Editor\Kismet\Private\BlueprintDetailsCustomization.cpp:446-454` |
| 编辑器上下文类型过滤器 | `IBlueprintEditor::GetPinTypeSelectorFilters(TArray<TSharedPtr<IPinTypeSelectorFilter>>&)` | `E:\UE_5.7\Engine\Source\Editor\Kismet\Public\BlueprintEditorModule.h:148` |
| 向蓝图编辑器注册 Tab | `FBlueprintEditorModule::OnRegisterTabsForEditor()`，事件签名 `(FWorkflowAllowedTabSet&, FName /*ModeName*/, TSharedPtr<FBlueprintEditor>)` | `BlueprintEditorModule.h:225-226` |
| 节点重建 | `UEdGraphSchema_K2::ReconstructNode(UEdGraphNode&, bool bUserAction)` | `EdGraphSchema_K2.h` |
| 编译 | `FKismetEditorUtilities::CompileBlueprint(UBlueprint*, EBlueprintCompileOptions, FCompilerResultsLog*)` | `E:\UE_5.7\Engine\Source\Editor\UnrealEd\Public\Kismet2\KismetEditorUtilities.h` |
| 编辑器刷新事件 | `FBlueprintEditor::OnRefresh()` | `E:\UE_5.7\Engine\Source\Editor\Kismet\Public\BlueprintEditor.h:1579` |
| 全局编译完成事件 | `GEditor->OnBlueprintCompiled()` | `UEditorEngine` |

### 1.4 社区调研：无现成方案

- GitHub 仓库搜索 `unreal blueprint variable type change` → 0 结果；`blueprint variable unreal` → 16 个，无一相关。
- Fab 上最接近的 **Blueprint Bulk Editor**（Sascha Wagentrotz）只批量改**默认值**、跨资产表格、CSV，不含类型修改，也不在蓝图编辑器内。
- 结论：自研编辑器插件，**不改引擎、不编译引擎源码**。

---

## 2. 用户已确认的决策

| # | 决策点 | 结论 |
|---|---|---|
| 1 | UI 形态 | 蓝图编辑器内新增可停靠面板（非工具栏弹窗、非独立全局窗口） |
| 2 | 变量范围 | 成员变量 **+** 函数/宏局部变量，面板含「范围」列 |
| 3 | 目标类型来源 | 原生标准类型选择器 **+** 「取类型自另一变量」（拷 `VarType`，含数组/容器/Object 类） |
| 4 | 引用与断线 | 一次总确认 + 自动重建节点 + 自动编译；不弹引擎的每变量确认框、不弹引用搜索窗；整批一次撤销 |
| 5 | 编译报错 | **不回滚**（改动落地，错误变量标红，仍可 Ctrl+Z 撤销） |
| 6 | Delegate 变量 | 列表内**灰显不可选**（引擎也不允许改其类型） |
| 7 | 局部变量默认值 | 表格里加标记，确认框里提示「默认值会被清空」 |
| 8 | 实现路线 | S1：自研面板 + 自研批量变更流程（复刻引擎字段处理，不用引擎那两个带弹窗的函数） |
| 9 | 面板文案 | 全中文（`LOCTEXT` + 中文默认文本），与 GASCombatBridge 风格一致 |

---

## 3. 面板形态与交互

**Tab**：TabId `BlueprintVariableBatchEditor`，默认停靠在蓝图编辑器 My Blueprint 附近，用户可拖到任意停靠区。每个蓝图编辑器一份实例，跟随当前编辑的蓝图。

**顶部工具条**：名称过滤框（即时、不区分大小写）、`全选 / 全不选 / 反选`、刷新按钮、右侧统计 `已选 3 / 共 47`。

**表格**（`SListView`，`ESelectionMode::Multi`，支持 Ctrl 点选 / Shift 连选 / Ctrl+A）：

| 列 | 内容 |
|---|---|
| 变量名 | 显示名 |
| 当前类型 | `UEdGraphSchema_K2::TypeToText()`，如 `Float`、`Array<Actor>` |
| 范围 | `成员` 或 `局部 · 函数名` |
| 引用 | 引用节点数，`> 0` 高亮 |
| 提示 | 局部变量显示 `默认值会重置`；Delegate 行显示 `不可改类型` |

**底部应用区**：
- 模式切换：`设置类型…` / `取类型自选中变量`
  - `设置类型…`：原生 `SPinTypeSelector`（同蓝图 Details 的控件与类型树，带当前编辑器上下文的 `GetPinTypeSelectorFilters` 过滤）
  - `取类型自选中变量`：在选中项里指定一个「类型模板」，其余选中变量拷它的 `VarType`
- `应用到选中 N 个` 按钮：无选中、或类型全部相同时置灰
- 状态栏：上次操作结果摘要（成功数 / 重建节点数 / 编译状态）

**不做**（本版）：双击跳转、拖拽排序、就地改名、批量改默认值、批量改 Category/复制标记等其它变量属性。

---

## 4. 数据来源与刷新

**行模型** `FVariableEntry`：
`FName Name`、`FEdGraphPinType Type`、`bool bIsLocal`、`TWeakObjectPtr<UFunction> Scope`、`TWeakObjectPtr<UK2Node_FunctionEntry> FunctionEntry`、`int32 RefCount`、`FString ScopeLabel`、`bool bIsDelegate`。行唯一键 = `(ScopeLabel, Name)`；成员变量 `ScopeLabel = "成员"`。

**成员变量**：读 `Blueprint->NewVariables`，逐个用 `FindNewVariableIndex()` 确认有效。SCS 组件变量、父类继承变量、Timeline 变量都不在 `NewVariables` 里，天然排除（与引擎 Details 的判断一致，见 `BlueprintDetailsCustomization.cpp:1594`）。`bIsDelegate` = `VarType.PinCategory == PC_Delegate`（与引擎 `!VariableProperty->IsA<FMulticastDelegateProperty>()` 等价）→ 灰显。

**局部变量**：遍历 `Blueprint->FunctionGraphs`，仅处理 `FBlueprintEditorUtils::DoesSupportLocalVariables(Graph)` 为真的图：
1. 优先 `Graph->GetSchema()->GetLocalVariables(Graph, OutVars)`；
2. 失败则取图上 `UK2Node_FunctionEntry` 节点，读 `LocalVariables` 数组（`SMyBlueprint.cpp:1283-1330` 同款双路径）；
3. scope = `FunctionEntry->FindSignatureFunction()`（`UFunction*`），即 `ChangeLocalVariableType` 需要的 `InScope`。
宏图无局部变量，不扫。

**引用统计（刷新时一次遍历）**：不逐变量调用 `GetNodesForVariable`（O(V×N) 会卡），改为：
- 收集 `Blueprint` + `GetLoadedChildBlueprints()` 的全部图（Ubergraph / Function / Macro / DelegateSignature）；
- 一次遍历所有 `UK2Node_Variable` 节点，按 `(VariableScope, GetMemberName())` 计数；
- 成员变量的引用数 = 该名字下所有 scope 的节点合计；局部变量 = 对应 scope 的节点数。
- 同时记录「来自子蓝图」的节点数，供确认框文案使用。

**刷新时机**（5 个入口）：Tab 激活（`SDockTab::OnTabActivated`）、`FBlueprintEditor::OnRefresh()`、`GEditor->OnBlueprintCompiled()`、本插件应用完成、手动刷新按钮。

**刷新时保留状态**：按 `(ScopeLabel, Name)` 匹配保留选中；保留滚动位置与过滤文本。蓝图 `bIsReadOnly` 或正在编译时禁用「应用」。

---

## 5. 批量应用流程

### 5.1 差异集

过滤掉「当前类型 == 目标类型」的选中项，余下为待改集合（N）。N = 0 时按钮置灰并提示「无变化」。

### 5.2 预检（不通过则整批拒绝并说明原因）

1. 目标类型合法：`PC_Object` / `PC_Interface` / `PC_SoftObject` 必须有有效 `PinSubCategoryObject` 且类型正确（引擎那里是 `check()`，我们改成给提示）。
2. 蓝图可编辑：`!Blueprint->bIsReadOnly`，且不在编译中。
3. 统计风险项：
   - `M` 个待改变量有引用，共 `K` 个节点，其中 `J` 处来自已加载子蓝图；
   - `W` 个变量改成 `Set`/`Map` 后会丢失复制标记（引擎 `ChangeMemberVariableType` 在容器化时静默清除 `CPF_Net`/`CPF_RepNotify`/`RepNotifyFunc`/`RepCondition`）；
   - `B` 个变量涉及 Bool ↔ 非 Bool（引擎会顺带重写 `FriendlyName`）；
   - `D` 个局部变量默认值将被清空（引擎 `ChangeLocalVariableType` 里 `Variable.DefaultValue.Empty()`）。

### 5.3 一次总确认框

自定义模态窗口，正文示例：

> 将修改 12 个变量（成员 9、局部 3）。其中 5 个被 21 个节点引用（3 处来自子蓝图），这些节点会被重建，与类型相关的连线将断开。2 个变量会失去网络复制标记。3 个局部变量的默认值将被清空。整批可用一次 Ctrl+Z 撤销。

按钮：`应用` / `取消`。不做「以后不再提示」。

### 5.4 执行

1. 单个 `FScopedTransaction`（整批一次撤销）；
2. `Blueprint->Modify()`，涉及局部变量再加 `FunctionEntry->Modify()`；
3. 逐个写入，复刻引擎逻辑：
   - `PC_Object`/`PC_Interface` 的类校验，`AActor` 类设 `CPF_DisableEditOnTemplate`，否则清除该标记；
   - Bool ↔ 非 Bool 时 `FriendlyName = FName::NameToDisplayString(VarName, bIsBool)`；
   - `Variable.VarType = NewPinType`；
   - `Set`/`Map` 时清 `CPF_Net`、`CPF_RepNotify`、`RepNotifyFunc`、`ReplicationCondition`；
   - 成员变量**不**重置默认值；局部变量清空默认值（与引擎一致）；
4. 全部写完后对本蓝图 + 受影响的已加载子蓝图各做**一次** `MarkBlueprintAsStructurallyModified`；
5. 对每个引用节点 `K2Schema->ReconstructNode(*Node, true)`（成员变量：本蓝图 + 已加载子蓝图；局部变量：所属图）；
6. 最后 `FKismetEditorUtilities::CompileBlueprint(Blueprint)` 一次。

### 5.5 结果与错误

- 状态栏：`成功 12 个 / 重建 21 个节点 / 编译通过`。
- 编译有错**不回滚**，出错变量名在表格标红，可展开查看编译错误摘要（`FCompilerResultsLog` 的 Error 行）。
- 不自动保存资产。

---

## 6. 边界与特殊情形

- **不进列表**：SCS 组件变量、父类继承变量、Timeline 变量（不在 `NewVariables`）。
- **灰显不可选**：`PC_Delegate`（事件分发器）变量。
- **同名遮蔽**：行键含 scope，成员与局部、不同函数的同名局部各自独立。
- **子蓝图**：已加载子蓝图的引用节点一起重建；未加载子蓝图不在本次处理范围，确认框注明「未加载的子蓝图未统计」。
- **拒绝执行**：蓝图只读 / 正在编译 / 目标类型非法。
- **适用蓝图类型**：所有走 `FBlueprintEditor` 的类型（Actor、组件、AnimBP、Widget、FunctionLibrary、Interface、EditorUtility），无 Actor 专有假设。
- **撤销**：整批一个事务；撤销后引擎可能把蓝图标记为需重新编译，属正常。

---

## 7. 工程结构

```
E:\UE_Plugins\BlueprintVariableBatch\
├── BlueprintVariableBatch.uplugin
├── README.md
├── 使用说明.md
├── docs\superpowers\specs\2026-11-06-blueprint-variable-batch-editor-design.md
└── Source\BlueprintVariableBatchEditor\
    ├── BlueprintVariableBatchEditor.Build.cs
    ├── Public\BlueprintVariableBatchEditor.h        # 模块：绑定/解绑 OnRegisterTabsForEditor
    └── Private\
        ├── BlueprintVariableBatchEditor.cpp
        ├── FVariableBatchTypes.h                    # FVariableEntry / 请求 / 结果结构
        ├── FVariableBatchCollector.h/.cpp           # 采集变量 + 单遍引用统计
        ├── FVariableBatchApplier.h/.cpp             # 预检 / 执行 / 结构修改 / 重建 / 编译
        ├── SVariableBatchPanel.h/.cpp               # 表格 + 工具条 + 应用区 + 状态栏
        ├── SVariableBatchConfirmDialog.h/.cpp       # 一次总确认框
        └── FVariableBatchTabFactory.h/.cpp          # FWorkflowTabFactory 实现
```

`.uplugin` 关键字段：`FriendlyName`「蓝图变量批量编辑器」、`Category` "Editor"、`EngineVersion` "5.7.0"、单模块 `BlueprintVariableBatchEditor`（Type=Editor，LoadingPhase=Default）、`CanContainContent=false`。

Build.cs 依赖：Core、CoreUObject、Engine、Slate、SlateCore、InputCore、UnrealEd、Kismet、KismetWidgets、BlueprintGraph、GraphEditor、EditorFramework、ToolMenus、Projects。

---

## 8. 构建与验证

宿主：`E:\UE_Plugins\_VerifyHost`（纯蓝图工程）。

1. 给 `VerifyHost.uproject` 增加 `"AdditionalPluginDirectories": [ ".." ]`（5.7 支持，`PluginManager.cpp:908` 读取；相对工程目录即 `E:\UE_Plugins`），并在 `Plugins` 中启用本插件；
2. 编译：
   ```
   E:\UE_5.7\Engine\Build\BatchFiles\Build.bat VerifyHostEditor Win64 Development -Project="E:\UE_Plugins\_VerifyHost\VerifyHost.uproject"
   ```
3. 启动编辑器按验收清单手测。

### 验收清单

1. Tab 出现在蓝图编辑器，切蓝图自动跟随；
2. 成员变量与局部变量都列出，范围列正确；
3. 名称过滤、Ctrl/Shift 多选、全选/全不选/反选、`已选 N / 共 M` 正确；
4. 单选改类型生效，引用节点重建，编译通过；
5. 批量改有引用变量：只弹一次确认框，数字准确，连线断开，节点重建，编译通过；
6. 「取类型自另一变量」可拷数组/容器/Object 类类型；
7. `Ctrl+Z` 一次撤销整批；
8. 局部变量改类型：默认值清空，表格有标记，确认框有提示；
9. Delegate 灰显不可选；SCS 组件变量、继承变量、Timeline 变量不出现；
10. 父子蓝图同时打开：父蓝图改变量类型 → 子蓝图引用节点重建；
11. 只读蓝图 / 编译中：应用按钮禁用并显示原因；
12. 200 变量规模刷新与操作不卡顿。

---

## 9. 里程碑

| 阶段 | 内容 |
|---|---|
| M1 | 插件骨架、模块绑定 Tab、面板可显示变量列表 |
| M2 | 引用统计、过滤、多选、统计显示 |
| M3 | 类型选择器、取类型模式 |
| M4 | 预检 + 确认框 |
| M5 | 批量应用 + 结构修改 + 节点重建 + 编译 + 撤销 |
| M6 | 结果报告、错误标红、刷新时机全覆盖 |
| M7 | 手测验收清单 + 中文使用说明/README |

---

## 10. 风险与对策

| 风险 | 对策 |
|---|---|
| 复刻引擎字段处理逻辑与引擎行为漂移 | 只复刻必要步骤，逐条对照 `BlueprintEditorUtils.cpp:5124-5290` 与 `5708-5800`，注释标出对应引擎行 |
| `SPinTypeSelector` 跨模块链接 | 引擎内 AnimGraph/GraphEditor 已跨模块使用；若链接失败，退回自绘类型下拉 |
| 批量后编译失败导致蓝图损坏感 | 不回滚 + 标红 + 可撤销；确认框提前说明断线风险 |
| 引用统计漏算未加载子蓝图 | 确认框与状态栏明确注明 |
| 面板在大蓝图卡顿 | 单遍统计 + `SListView` 虚拟化；只在 5 个时机刷新 |

---

## 11. 实现与验证记录

- 2026-11-06 实现完成，源码位于 `E:\UE_Plugins\BlueprintVariableBatch\Source\BlueprintVariableBatchEditor`；UBT 编译 `Result: Succeeded`（VS2022 14.44 / Windows SDK 10.0.22621，增量约 2~5 秒）。
- 验证宿主 `E:\UE_Plugins\_VerifyHost` 已改造成最小 C++ 工程（`Source\VerifyHost.Target.cs`、`Source\VerifyHostEditor.Target.cs`、`Source\VerifyHost\*`），插件通过工程内 `Plugins` 目录联接被 UBT 识别；`VerifyHost.uproject` 里显式禁用引擎 Marketplace 下的同名二进制插件 `GASCombatBridge`，否则 UBT 报 `Could not find definition for module 'GASCombatBridge'`。
- 自动化测试（`UnrealEditor-Cmd ... -ExecCmds="Automation RunTests BlueprintVariableBatch;Quit"`）：
  - `BlueprintVariableBatch.BatchChangeType`：成功（收集 → 计划 → 应用 → 类型校验 → "无变化"判定）。
  - `BlueprintVariableBatch.TabSpawn`：成功（打开蓝图编辑器后 Tab 自动出现并生成面板；NullRHI 环境下自动跳过窗口相关断言）。
- 与设计稿的偏差及原因：
  1. 确认框改用 `FMessageDialog::Open(EAppMsgType::OkCancel, ...)`（按钮为「确定/取消」），未自绘 `SVariableBatchConfirmDialog`：引擎的 `SModalDialogWithCheckbox` 只有前向声明、定义在 .cpp 内不可复用，自绘 modal 窗口性价比低。
  2. 插件模块 `LoadingPhase` 定为 `PostEngineInit`：引擎 `FBlueprintEditorModule::StartupModule()` 内 `check(GEditor)`，`Default` 阶段加载会在编辑器初始化前触发断言崩溃。
  3. 引用统计规则：成员变量按「变量名相同即计数」镜像引擎语义（含其它函数里的同名局部变量节点），局部变量按「函数作用域 + 名字」计数。
  4. `FBlueprintEditorUtils::GetNodesForVariable` / `GetLoadedChildBlueprints` 在 5.7 属于 protected，改为 `FVariableBatchUtils.h` 内联复刻（与引擎实现逐行一致）。
  5. 构建计划时以蓝图里的「当前类型」为准，而不是面板缓存的行数据，避免面板数据过期导致重复修改。
- 仍需人工验收的交互项：第 8 节清单中的多选操作手感、原生类型选择器交互、确认框文案与数字、`Ctrl+Z` 整批撤销、父子蓝图联动、只读/编译中禁用、200 变量规模下的流畅度。
---

## 12. 增量需求：批量修改变量类别（2026-11-06 追加）

### 12.1 需求与决策

用户原始诉求（m00971）：「我在开发时喜欢把变量分门别类，所以我希望，这里的类别也可以批量更改，把这个功能加入到同一个窗口可行吗？」

已确认的 4 项决策（m00990）：

| 编号 | 问题 | 结论 |
| --- | --- | --- |
| 1 | 类别区放在哪 | **面板底部独立区块**：类型区保持原样，下方新增「修改变量类别」区，各自独立的「应用」按钮 |
| 2 | 适用范围 | **只做成员变量**：局部变量不参与「我的蓝图」分组，批量改类别时跳过并在确认框/状态栏注明数量 |
| 3 | 类别输入方式 | **可编辑文本框 + 本蓝图已有类别下拉建议**；支持多级类别（用竖线 `\|` 分隔），可新建类别 |
| 4 | 是否需要「取类别自选中变量」模板模式 | **不需要** |

### 12.2 引擎 API 依据

- `FBlueprintEditorUtils::SetBlueprintVariableCategory(UBlueprint* Blueprint, const FName& VarName, const UStruct* InLocalVarScope, const FText& NewCategory, bool bDontRecompile = false)`：`BlueprintEditorUtils.h:1189`，实现 `BlueprintEditorUtils.cpp:4058-4137`。空类别 → `UEdGraphSchema_K2::VR_DefaultCategory`；与旧类别 `EqualTo` 直接返回；自带 `FScopedTransaction` + `Blueprint->Modify()`；在 `SkeletonGeneratedClass` 上找到属性且不是原生变量时写 `TargetProperty->SetMetaData(TEXT("Category"), ...)`；同步 `Blueprint->NewVariables[VarIndex].Category`；最后 `MarkBlueprintAsStructurallyModified`。
- `FBlueprintEditorUtils::GetBlueprintVariableCategory(UBlueprint*, const FName&, const UStruct* InLocalVarScope)`：头文件 `:1232`，实现 `:4296-4324`（骨架类元数据 → 局部变量 → SCS 节点）。
- 默认类别 `UEdGraphSchema_K2::VR_DefaultCategory` 在 `EdGraphSchema_K2.h:455`；多级类别分隔符为 `|`（`SMyBlueprint.cpp:244` 按 `|` 切根类别）。

### 12.3 面板改动

- 表格新增「类别」列（位于「当前类型」之后）：成员变量显示当前类别（空显示 `Default`），局部变量显示 `—`。
- 底部新增独立区块：
  - 标题「修改变量类别（只对成员变量生效）」；
  - `SEditableTextBox`（提示「留空 = Default；多级类别用 | 分隔，例如 战斗|伤害」）；
  - 「已有类别…」`SComboBox`，选项来自本蓝图里已经用过的类别（去重排序）；
  - 「应用类别到选中变量」按钮：仅当有选中成员变量、蓝图可编辑、且至少一个变量的类别与目标不同时可用。

### 12.4 应用流程（`FVariableBatchApplier` 新增三个函数）

1. `BuildCategoryPlan(UBlueprint*, const TArray<TSharedPtr<FVariableBatchEntry>>&, const FText& RequestedCategory)`：
   - 蓝图不可编辑 → `FailureReason`；
   - 目标类别 `TrimStartAndEnd()` 为空 → `VR_DefaultCategory`；
   - 逐个候选：局部变量 → `NumSkippedLocals++` 并跳过；否则用 `GetBlueprintVariableCategory` 读**蓝图里的当前类别**（空视为 `Default`），与目标 `EqualTo` 则跳过；
   - 无变化时 `FailureReason = 「选中的成员变量已经都在该类别下，没有需要修改的变量。」`。
2. `ConfirmCategoryPlan(...)`：`FMessageDialog::Open(EAppMsgType::OkCancel, ...)`，正文含变量数量、目标类别、跳过局部变量数、说明「类别只影响编辑器分组、不改变运行时布局、不重建节点」以及「整批可用一次 Ctrl+Z 撤销」。
3. `ApplyCategoryPlan(...)`：
   - 单个 `FScopedTransaction("批量修改变量类别")` + `Blueprint->Modify()`；
   - 逐条 `FindNewVariableIndex` → 写 `NewVariables[Index].Category` → 同步骨架类属性 `Category` 元数据（`Cast<UClass>(TargetProperty->GetOwnerStruct())`，`OuterClass == nullptr || ClassGeneratedBy == nullptr` 视为原生变量，跳过写元数据）；
   - 全部写完后 `MarkBlueprintAsStructurallyModified` 一次；**不重建节点、不额外调用 `CompileBlueprint`**（引擎会在帧末自动重编译）；
   - 状态栏：`已将 N 个变量归入类别「X」。`

### 12.5 验证

- 新增自动化测试 `BlueprintVariableBatch.BatchChangeCategory`（`FVariableBatchTests.cpp`）：临时蓝图 3 个 int 成员变量 → 编译生成骨架类 → 收集 → 生成「战斗|伤害」计划（断言 3 个待改）→ 应用（断言修改 3 个）→ 断言 `NewVariables` 与 `GetBlueprintVariableCategory` 均为目标类别 → 二次应用判为无变化 → 空输入回落 `VR_DefaultCategory` 并成功应用。
- 运行结果：`Automation RunTests BlueprintVariableBatch` 共 3 个测试（BatchChangeCategory / BatchChangeType / TabSpawn）全部 `Result={成功}`，进程 exit code 0。
- 仍需人工验收：类别区布局与「已有类别…」下拉交互、多级类别在「我的蓝图」里的分组效果、一次 Ctrl+Z 撤销、只读/编译中禁用。