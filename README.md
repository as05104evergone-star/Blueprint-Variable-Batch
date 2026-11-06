# 蓝图变量批量编辑器（Blueprint Variable Batch）

在蓝图编辑器里**多选变量、一次性统一修改类型或类别**的 UE 编辑器插件。

UE 自带的「我的蓝图（My Blueprint）」面板是单选列表，Details 面板一次只能编辑一个变量，因此"给十几个变量统一换类型"只能逐个点、逐个确认、逐个搜索引用——这也是本插件存在的原因。

- 引擎版本：**UE 5.7**（开发与验证于 5.7.4）
- 插件类型：编辑器插件（`Editor` 模块，`PostEngineInit` 阶段加载）
- 依赖：无第三方依赖，仅使用引擎自带模块

## 功能

- 在任意蓝图编辑器（Actor / 组件 / 动画蓝图 / 控件 / 函数库 / 接口 / 编辑器工具蓝图等）中新增一个可停靠标签页「**变量批量编辑器**」。
- 表格一次列出该蓝图的**成员变量**与**函数 / 宏的局部变量**，并显示：当前类型、所属范围、引用节点数、注意事项。
- 支持 Ctrl 点选、Shift 连选、Ctrl+A 全选；可按名称过滤；支持全选 / 全不选 / 反选。
- 目标类型两种来源：
  1. 使用引擎原生类型选择器（与 Details 面板同一个控件，含蓝图编辑器提供的类型过滤）；
  2. 「取类型自选中变量」——从面板里选一个变量当模板，复制它的完整类型（含数组 / 容器 / 对象类）。
- 一次点击应用给所有选中变量：
  - 一个事务包住整批修改，**一次 Ctrl+Z 即可整体撤销**；
  - 只触发一次蓝图结构修改、一次编译，比"逐个改"快得多；
  - 自动重建所有引用这些变量的节点；
  - 编译若有错误**不回滚**，错误会列在「编译器结果」标签里。
- 应用前会弹一次汇总确认框，说明本次会修改多少变量、影响多少引用节点（其中多少来自已加载的子蓝图）、多少个变量会失去网络复制标记、多少个局部变量的默认值会被清空等。
- 表格里额外显示每个变量在「我的蓝图」中所属的**类别（Category）**；面板底部的「修改变量类别」区可以把选中的一批成员变量一次归入同一个类别（支持 `|` 多级类别，留空即 `Default`），同样一次 Ctrl+Z 整体撤销。

## 与引擎「逐个修改」行为的一致性

插件复刻了引擎 `FBlueprintEditorUtils::ChangeMemberVariableType` / `ChangeLocalVariableType` 的字段处理，因此行为与在 Details 里单个修改一致：

| 情况 | 行为 |
| --- | --- |
| 成员变量的默认值 | 保留，不重置 |
| 局部变量的默认值 | 与引擎一致——**清空**（面板里会提前用「默认值会重置」提示） |
| Bool ↔ 非 Bool 互转 | 按引擎规则重算变量显示名（FriendlyName） |
| 改成 Set / Map | 与引擎一致，清除 `CPF_Net` / `CPF_RepNotify` / RepNotify 函数 / 复制条件 |
| 改成 Actor 类对象类型 | 与引擎一致，设置 `CPF_DisableEditOnTemplate` |
| 有节点引用该变量 | 自动重建这些节点（包括已加载子蓝图里的引用） |

差别只有一处：**不会**在每个变量上弹"可能断开连接，是否搜索引用"的窗，也**不会**为每个变量打开一次搜索窗口——风险在应用前一次性汇总告知。

## 批量修改变量类别

除了类型，插件还能批量修改变量在「我的蓝图」里的分组类别：

- 表格的「类别」列显示每个成员变量当前所属类别（空类别显示为 `Default`，局部变量显示为 `—`）。
- 底部「修改变量类别」区：直接在文本框里输入类别名（支持 `|` 分隔的多级类别，例如 `战斗|伤害`；留空表示 `Default`），也可以从「已有类别…」下拉里挑一个本蓝图已经用过的类别。
- 点击「应用类别到选中变量」后，会把所有选中的**成员变量**一次改成该类别；局部变量不参与「我的蓝图」分组，会自动跳过并在确认框里注明。
- 类别只影响编辑器里的变量分组，不改变运行时数据布局，也不会重建节点或额外触发一次编译；整批修改同样是一个事务，一次 Ctrl+Z 即可撤销。

## 为什么没有现成工具能做这件事（2026-11 调研）

写这个插件之前先确认过：UE 5.7 编辑器里确实做不到「多选变量 + 批量改类型」，这不是操作方式的问题——

1. **引擎把「我的蓝图」列表写死成单选。** `SMyBlueprint` 使用的 `SGraphActionMenu`，其内部 `STreeView` 的选择模式固定为 `ESelectionMode::Single`（`SGraphActionMenu.cpp`），没有任何参数或 setter 能在**不改引擎**的前提下打开多选；即便多选，Details 面板的派发逻辑也只取 `SelectedActions[0]`。
2. **官方论坛的同类需求从 2017 年提到 2025 年仍未实现。** [Feature Request - Select multiple variables](https://forums.unrealengine.com/t/feature-request-select-multiple-variables/402926)——说明这是长期缺口，而不是「已经有人做了但没找到」。
3. **商业插件里最接近的几个都不做类型：**
   - [Blueprint Bulk Editor](https://www.fab.com/listings/00d52b04-ac61-440c-9380-03d4169f7a93)：跨资产表格批量改**默认值**、支持 CSV 导入导出，不改类型；
   - [Blueprint Variable Cleaner](https://www.fab.com/listings/f5567866-a9f2-4e5d-bcf2-ba7691943030)：批量打开蓝图、清理无用变量，不改类型；
   - [Property Transfer Tool](https://unrealbox.pro/asset/1689)：在资产之间搬属性，不改类型。
4. **GitHub 上没有公开的同类开源项目**（截至 2026-11）：仓库搜索无相关结果；代码搜索 `ChangeMemberVariableType` 只命中引擎源码 fork，以及 RigVM / ControlRig 里同名但用途不同的内部 API，没有插件在包装它。

所以本插件没有走「调用现成接口」的捷径，而是自建面板 + 复刻引擎 `FBlueprintEditorUtils::ChangeMemberVariableType` / `ChangeLocalVariableType` 的字段处理，把「每个变量一次弹窗、一次搜索、一次编译」合并成「一次确认、一次重建、一次编译」。

如果你知道有同类作品（开源或商业），欢迎开 issue 告诉我，我会把它补到这里。

## 安装

方式一：把整个 `BlueprintVariableBatch` 目录复制到工程的 `Plugins/` 下（或引擎的 `Engine/Plugins/` 下），然后编译工程。

方式二（源码工程 / 需要 C++ 工具链）：把目录放到工程 `Plugins/` 下，用 UBT 编译：

```
"<引擎目录>/Engine/Build/BatchFiles/Build.bat" <你的工程名>Editor Win64 Development -Project="<你的工程>.uproject" -WaitMutex
```

编译成功后启动编辑器，在「编辑 → 插件」里确认「蓝图变量批量编辑器」已启用。

> 注意：本插件是**源码插件**，需要已安装 Visual Studio 2022（含 C++ 桌面开发）+ Windows SDK 的 C++ 工程环境；纯蓝图工程首次编译时会提示创建 C++ 模块，确认后即可。

## 使用

1. 打开任意蓝图资产。
2. 在「我的蓝图」标签旁找到「**变量批量编辑器**」标签（没有的话见下面「常见问题」）。
3. 按需过滤 / 勾选变量（表头可看每个变量的当前类型、范围、引用数、提示）。
4. 底部选择目标类型：直接选「统一设置为」的类型，或切到「取类型自选中变量」并指定模板变量。
5. 点击「应用到选中 N 个变量」，阅读汇总确认框后点「确定」。
6. 查看状态栏结果与「编译器结果」标签；需要撤销时按一次 Ctrl+Z。

详细图文步骤与常见问题见 [使用说明.md](使用说明.md)。

## 已知限制

- 「事件分发器」（Delegate / Multicast Delegate）变量的类型不可更改：表格中灰显、无法选中（与引擎 Details 面板的限制一致）。
- 组件的 SCS 变量、父类继承来的变量、Timeline 变量不会出现在列表中——它们不在蓝图的 `NewVariables` 里，类型本来也不允许在此修改。
- 未加载的子蓝图不会被处理（应用前确认框会注明有多少引用来自已加载子蓝图）。
- 改动后不自动保存资产，也不自动关闭搜索窗口之外的东西；保存由你自己决定。
- 蓝图正在编译、或在源码控制中被标记为只读时，会拒绝执行。

## 自动化测试

插件内附三个自动化测试（`WITH_DEV_AUTOMATION_TESTS`）：

- `BlueprintVariableBatch.BatchChangeType`：临时蓝图 → 收集变量 → 生成计划 → 应用 → 校验类型与二次应用判定。
- `BlueprintVariableBatch.BatchChangeCategory`：临时蓝图 → 生成类别计划 → 应用 → 校验 `NewVariables` 与类别查询 API、空输入回落 `Default`、二次应用判为无变化。
- `BlueprintVariableBatch.TabSpawn`：打开临时蓝图编辑器，确认标签页可以被打开并生成面板（NullRHI 环境下自动跳过）。

在编辑器里运行：**工具 → 测试自动化（Session Frontend）**，搜索 `BlueprintVariableBatch` 后执行。

## 目录结构

```
BlueprintVariableBatch/
├── BlueprintVariableBatch.uplugin
├── README.md
├── 使用说明.md
├── Source/BlueprintVariableBatchEditor/
│   ├── BlueprintVariableBatchEditor.Build.cs
│   ├── Public/BlueprintVariableBatchEditor.h
│   └── Private/
│       ├── BlueprintVariableBatchEditor.cpp   # 模块：注册 Tab 与布局扩展
│       ├── FVariableBatchTabFactory.h/.cpp    # 标签页工厂
│       ├── SVariableBatchPanel.h/.cpp         # 面板与表格行
│       ├── FVariableBatchCollector.h/.cpp     # 收集变量与引用统计
│       ├── FVariableBatchApplier.h/.cpp       # 预检 / 确认 / 批量应用 / 编译
│       ├── FVariableBatchTypes.h              # 数据结构
│       ├── FVariableBatchUtils.h              # 引擎 protected 工具的本地复刻
│       └── FVariableBatchTests.cpp            # 自动化测试
└── docs/superpowers/specs/                    # 设计与验收依据
```

## 开发说明

- 模块类 `FBlueprintVariableBatchEditorModule` 在 `StartupModule` 里绑定 `FBlueprintEditorModule::OnRegisterTabsForEditor()` 与 `OnRegisterLayoutExtensions()`，因此对**所有**蓝图编辑器模式都可用（图模式、默认值模式、组件模式、接口模式、宏模式）。
- 因为引擎 `FBlueprintEditorModule::StartupModule()` 里断言 `GEditor` 必须存在，插件模块的加载阶段设为 `PostEngineInit`；命令行 cook / 无编辑器进程会自动跳过绑定。
- C++ 源码为 **UTF-8 带 BOM**（中文注释与字符串在 MSVC 下需要 BOM 才能正确编译），修改时请保留。

## 许可

[MIT](LICENSE) © 2026 Chives
