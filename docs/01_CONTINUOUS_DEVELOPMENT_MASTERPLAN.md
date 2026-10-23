# FEM Solver Database V2 — 空闲时段连续开发、批次双审与集中修复实施总计划

**版本**：2.0 / 2026-10-23  
**目标仓库**：https://github.com/Nidofy/fem-solver-database  
**实施模式**：GLM-5.3 持续开发 → 冻结批次 → GPT-6 Astra + Claude Opus 5.5 独立综合审核 → GLM-5.3 集中修复 → 验收合并 → 下一批次。  
**资料依据**：用户提供的 `MODERNIZATION_ROADMAP.md`、上一版 `SDB_V2_EXECUTION_MASTERPLAN.md` 及上一轮仓库源码审查记录。当前实际 HEAD、环境、测试情况须由 GLM 在执行时重新确认；本文不是新的性能实测声明。

## 0. 本轮变更：从“每任务外审”转为“持续开发 + 批次审核”

核心目的不是取消审核，而是把审核放在合理的集成切面，以提高空闲模型额度利用率：

- **每项开发任务**：GLM 实现、完成相关测试、提交原子 Commit、记录证据，然后自行推进下一个 READY 任务；不等待 Astra/Opus。
- **每个 Review Batch**：GLM 打包多个已自测任务（默认约 4–8 项；以模块耦合度和风险为准，而非死板数量），固定 SHA 与测试证据，然后由 Astra 与 Opus 并行、彼此独立地审查整个批次。
- **返工阶段**：GLM 汇总两份报告，形成唯一 Finding 台账，优先修 C0/C1，添加反例与回归测试；仅对增量修复和关联风险做定向复审，不默认再做两遍完整审核。
- **稳定线**：只有综合审核完成且验收门槛达标的批次，才能进入受保护稳定分支。未审核的代码允许存在于开发分支，但不得标记为生产可用。
- **运行限制**：一个 Codex/DSH Agent 的实际执行受会话、工具、用量和权限限制。Prompt 指挥其**在当前可运行的会话内尽可能连续推进**，而不是保证无人操作时无限期运行；在会话结束前形成可复制的 Relay/Resume Prompt。

> 与上一版最关键的差异：原计划的每个任务 Review Gate 改为“开发自测门”，原计划 C0/C1 等质量门仍保留在**批次验收**处。架构断点是例外，仍需提前决策。

## 1. 代码边界与已知基础

沿用现有 C++20 Solver Database V2：`Field/SoA/AoSoA`、`MeshDB/StateDB/ContactDB`、ModelIR/Abaqus INP 子集、显式 TET4 CPU、`sdb_run`、`.sdbmodel`/`.sdbres`/`.sdbrst`、Results Reader、VTK/XDMF、通用接触参考模块、CUDA 源码。以上是**现有开发基础**，不是新的实测或工业化完成结论。保持现有接口和测试路径，严禁未经证据重建一套平行 Database。

真实问题及优先级（基于用户附件及上一轮源码审查）：

1. `sdb_run.cpp` 每步调用结果发布，`ResultPublisher::publish()` 早于输出调度执行 `cpu.forces()`：应消除非输出步无谓计算。**现有 Writer 已跳过空 Field 帧，不可虚构“空帧写入磁盘”错误。**
2. Reader 初始化全扫及 History 逐帧查询，对长时间历程不友好：需版本化结果索引与流式读取。
3. 8T float 性能效率与 View/raw 开销存在附件记录的瓶颈；需要在本机重新复现实验，建立公平统计口径。
4. CUDA 代码存在不等于已在真实 GPU 上通过；目前缺少可信设备验收证据。
5. 面向工业模型必须逐项扩展 ElementDescriptor、Section、INP 语义、材料历史与真实物理 Kernel，不以拓扑枚举数量冒充支持程度。

## 2. 长期合作与角色

| 角色 | 唯一责任 | 不允许行为 |
|---|---|---|
| GLM-5.3 主开发 | 选择 READY 任务，提交代码/测试/记录；组织 Batch；修复双审意见 | 自动合并受保护稳定分支、虚构性能或 CUDA 结果、修改无关生产求解器 |
| GPT-6 Astra Reviewer-A | 跨任务整体架构、生命周期、内存、缓存、并行、CUDA/CPU、结果存储与性能审核 | 修改 GLM 工作树、先看 Opus 报告而丧失独立性 |
| Claude Opus 5.5 Reviewer-B | 有限元公式、时间层、材料、接触、输入语义、物理正确性、数据完整性对抗审核 | 修改 GLM 工作树、用接口存在代替物理能力验证 |
| 用户/集成负责人 | 设置开发范围与每批审核时机、解决无法自动决策的外部依赖或高风险架构冲突、批准稳定线合并 | 不需逐个低风险任务人工批准 |

初审时 Astra 和 Opus 互不看对方结论；两份报告齐全后，GLM 整合并处理。

## 3. 推荐 Git 分支与冻结规则

实际仓库分支和保护规则先由 GLM 读取，不预设 `main` 必然存在。

```text
stable (仓库当前已验收基线/被保护的目标分支)
  └── dev/glm-b01          # GLM 连续工作，任务原子提交
       ├─ commit R0.01
       ├─ commit R0.02
       ├─ commit R0.04
       ├─ commit R1.01
       └─ commit R1.02
            ↑ review/b01   # 标注不可变 BATCH_HEAD_SHA
            ├── review worktree Astra (read-only)
            └── review worktree Opus (read-only)
       ├─ fix B01-C1-01
       └─ fix B01-C2-03
            ↑ FIX_HEAD_SHA
  └── accepted batch B01 → stable
  └── dev/glm-b02 (从新的 stable HEAD 出发)
```

- 每个任务均保留可读 Commit，主开发只写自己的 worktree。Reviewer 使用冻结 SHA 的独立只读工作树；报告可另存文件或提交到独立 reviews 文档分支。
- `BATCH_BASE_SHA`、`BATCH_HEAD_SHA`、`FIX_HEAD_SHA`、`ACCEPTED_MERGE_SHA` 必须明确区分；审查不允许读取变化中的 HEAD 并仍称其为同一快照。
- 默认**批次审核期间暂停在同一分支继续产生依赖性代码**，让返工可定位。若审查尚未启动而 GLM 仍有大量空闲额度，允许启用 `EXTENDED_IDLE`：先冻结 Bxx，再以最后已验收的稳定 SHA 在**另一独立 worktree**继续开发与 Bxx 无依赖、无重叠文件的测试/Bench/隔离功能原型，形成候选 Bxx+1；绝不基于未审核的破坏性 Schema 或物理状态继续层层开发，也不能自动合并 stable。用户随后可以集中启动数个固定快照的审查，但每个快照都必须明确其 BASE/HEAD，不把不同候选混成一个不可信 Diff。
- 禁止 `reset --hard`、`clean -fdx`、强制推送或覆盖用户尚未提交的工作；不在生产求解器原仓库修改代码。

## 4. GLM 的“空闲时间自动推进”算法

每次 GLM 长运行按如下动作重复，直至达到停止条件：

```text
Read AGENTS.md / development state / Git status
              ↓
选择依赖均满足的 READY 任务（风险较低、范围可控优先）
              ↓
阅读实际源码与测试 → 写最小失败用例 → 实现 → 局部测试
              ↓
原子 Commit + test-evidence + Task Ledger 更新
              ↓
每 2–3 个任务执行一次 CPU 全量 CTest；每批末执行完整适用验证
              ↓
检查 Batch 预算与停止信号
      ├─ 还能安全继续：取下一个 READY 任务
      ├─ 遇到环境阻断：记录 BLOCKED_ENV，换独立 READY 任务
      ├─ 重大设计决策：冻结 ADR/实验实现，停止受影响链，转独立安全任务
      └─ 达到批次上限/无安全任务：冻结 SHA，生成 Review Packet
```

### 默认任务批次大小

- 每批建议约 **4–8 个关联任务**；这只是审核复杂度的经验值，不是 GLM 本次会话的人工截止。低风险 CI/Bench/测试可扩到 8–12 项；大规模的物理内核/Schema 改动可缩为 1 项一批。原则上围绕 **1 个主要子系统 + 1 个支撑领域**，例如“输出调度 + Result/Restart 正确性”。
- 对纯测试、Bench/CI 可更多；对物理 Kernel、文件 Schema、Contact history、CUDA 异步同步等**一个重大高风险改动即可单独构成 Batch**。
- 不建议一个批次跨越 Core Field 布局、INP 编译器、接触物理、结果文件格式等 3 个以上边界，否则综合审核会很难全面覆盖。
- 如果 Diff 已大到审查者难以逐个检查核心路径，提前冻结。代码行数只作辅助信号，不用单一 LOC 硬门槛。
- 每次运行应尽量推进至**可运行的功能切片**：不仅是接口，还应包括测试、使用场景、数据读写闭环。

### 开发自主权与停机规则

GLM 可自行进行：修 bug、加回归测试、优化已有算法、局部重构、补诊断/CI、受控实验性实现。必须满足可回退、可对照、有实际运行证据。

需要提前停在 ADR 或隔离 Feature Flag 的事项：

- 改变 `.sdbmodel` / `.sdbres` / `.sdbrst` 的破坏性 Schema、丢失旧版读取或模型指纹含义；
- 改变显式时间层、材料 Trial/Commit、接触持久 ID、全局自由度语义；
- 将新版算法切换为默认路径但尚未提供独立物理验证；
- 直接修改现有生产求解器、引入有许可证约束的依赖或需要外部保密资产；
- 修改 ABI/跨模块契约且导致后续多个任务锁定于一个未经验证的决策。

注意：这些事项仍可让 GLM **预研、加独立 Prototype 和测试**，不是要求停止全部开发；只是不能把高风险实现推到稳定路径或在其基础上无限堆积后续工作。

## 5. 五条开发主线与依赖

| 主线 | 工作范围 | 启动条件 | 早期交付目的 |
|---|---|---|---|
| A — Correctness / Lifecycle | R0 基线、R1 输出调度/时间层、Restart/Result 完整性 | 立即 | 可复现且状态一致的全生命周期闭环 |
| B — CPU / Cache | R2 线程调度、View/raw、TetPlan，R3 SIMD/AoSoA/NUMA | R0 真实基准 | 在不破坏结果的前提下获得实测加速 |
| C — ResultDB / IO | R4 索引、压缩、随机 History、崩溃恢复和异步队列 | R1 契约稳定 | 大量帧可高效存取和后处理 |
| D — Input / Physics | R5 Descriptor、INP、C3D8/C3D8R；R6 材料/载荷/NLGEOM；R7 接触 | R0、对应语义依赖 | 真实工程模型形成“输入-计算-结果”纵向闭环 |
| E — Device / Scaling | G0 CUDA 真设备 → G1 单卡常驻 → 后续多卡/MPI/隐式 | CUDA 工具链/物理内核门槛 | 从代码存在转为真实设备上的全路径证明 |

避免人为把所有内容锁为单条串行链；然而 **单个 GLM 是唯一写入者**，这些是可切换的任务轨道，不代表同一工作树并发修改。

## 6. 推荐 Review Batch 队列（执行时按实际进度调整）

以下是建议的功能批次；编号不暗示已经完成。

| 批次 | GLM 自主任务池 | 主要产出 | 冻结条件与专项验收 | Reviewer 重点 |
|---|---|---|---|---|
| **B01** | R0.01–05 + R1.01/R1.02 的安全子任务 | SHA、CTest、Bench、工具链状态、输出提前调度 | CPU 原有用例、无输出时无多余 force、时间层基准与重启动一致 | Astra 性能口径；Opus 输出物理时间层 |
| **B02** | R1.03/R1.04 + R2.01/02/04/05 | IO 故障注入、P/E 核实验、View/raw、初始化优化 | 同条件统计，真实 8T 对照，错误文件检测/拒绝 | Astra 为主，Opus 验证 IO/Restart 不变量 |
| **B03** | R4.01–R4.04（必要时拆 B03a/B03b） | ResultDB v3 的持久索引、随机查询、读取兼容 | v1 读取、损坏索引重建、100k Frame 元数据压测、内存预算 | Astra + Opus 必审 Schema 和恢复 |
| **B04** | R4.05–07 + 完整生命周期扩展 | zstd 块、增量写、恢复/导出，改进历史索引 | 中断写入、CRC、丢帧策略、重启追加和查询 | Astra + Opus 综合审核 |
| **B05** | R5.01/02 + INP/Section 完整 C3D8 | Descriptor → INP → C3D8 → ResultReader | Patch/负 Jacobian/能量/反力/float-double | Opus 主审物理，Astra 数据布局 |
| **B06** | R5.03/04/05 | C3D8R 缩减积分、沙漏与输出 | Hourglass 模态、能量比、Reference | Astra + Opus 双审 |
| **B07** | R3.01–R3.03 或 R2 的剩余性能诊断 | SIMD/AoSoA、缓存 tile、ISA 回退 | 相同模型与精度性能，对照 Scalar oracle | Astra 主审、Opus 查数值误差 |
| **B08** | R6.01/R6.02 | 材料历史 Schema + J2 Plasticity | 加载卸载/耗散/Restart/结果状态 | Opus 主审，Astra 查历史与数据归属 |
| **B09** | R6.03–05（可拆） | NLGEOM、随动载荷、显式时间步改进 | 刚体转动/客观性/能量/稳定性 | Astra + Opus |
| **B10** | R7.01–05（应拆成多个批次） | 持久 BVH、搜索、摩擦历史、接触结果 | 接触反例、确定性/性能、摩擦耗散 | Astra + Opus |
| **G00** | G0.01（可和 B01 并行做环境探测） | 真机 CUDA 编译及 TET4 设备数值验证 | 设备/工具链/日志真实存在，否则 BLOCKED_ENV | Astra + Opus |
| **G01** | G1.01–03（按物理域拆分） | 单卡 GPU 常驻计算/HEX8/接触逐项推进 | 无每步隐式 Host 下载；CPU/GPU 容差与全路径计时 | Astra + Opus |
| **长期** | R8 壳梁、高阶；R9 MPI；R10 隐式；R11 多格式 | 逐族完整垂直切片，独立计划 | 每批明确物理/数据/设备/结果范围 | 风险分级双审 |

**调度推荐**：B01 → B02；之后 B03/B04 与 B05/B06 按可用测试环境和代码依赖选择。ResultDB 不必等 CPU SIMD 做完；G00 一有合适 GPU 环境即可插入。不要为追求“任务完成数量”同时开启 6 条架构主线。

## 7. 每个任务不等双审，但必须满足自测 DoD

GLM 在 Task Ledger 中标 `IMPLEMENTED_SELF_TESTED` 前，至少确保：

1. 有真实最小测试/反例，当前机器上适用的构建与测试已执行，原命令、退出码、环境留存；
2. 未破坏已接受的旧生命周期与 Result/Restart 兼容路径；
3. 若涉及数值算法，保留独立参考解/物理约束，不以“运行不崩溃”为证明；
4. 若涉及性能，基准条件相同，区分 Cold-start / Steady-state / IO-included，报告中位数或分布及误差；
5. 未验证 CUDA/Linux/MPI 等如实写 `BLOCKED_ENV`，不能写 PASSED；
6. 一个任务一个可审查 Commit（或少量有说明的 Commit），本任务文件更新、未提交更改范围清楚。

GLM 可继续下一任务，但这些任务在批次审核前只具备 `IMPLEMENTED_SELF_TESTED` 身份，不等于最终 `ACCEPTED`。

## 8. Review Batch 冻结包（必须能脱离开发线程审查）

每个 Batch 在 `verification/reviews/<BATCH_ID>/` 准备：

```text
manifest.yaml              # ID、BASE/HEAD、任务范围、父依赖、风险、变更文件
review-packet.md           # 设计说明、调用链、破坏点、详细 Diff 说明
build-test.md              # 真正执行的命令、环境、返回码、日志路径
benchmark.json             # 若改变性能，旧新对照与机器配置
numerical-validation.md    # 物理/时间层/容差/长期漂移/参考解
compat-matrix.md           # INP/Model/Result/Restart/API old/new
open-risks.md              # 未完成、无法验证、实验分支与回退开关
astra-review.md            # 审核完成后录入；初始不可伪造
opus-review.md             # 审核完成后录入；初始不可伪造
findings.md                # 合并审核及修复状态
repair-report.md           # GLM 修复和定向复审证据
```

任何报告必须引用固定 SHA，不能以移动中的工作树代替提交快照。显式区分本次实测和历史数据。

## 9. 双审、集中修复与合并规则

**Astra 检查跨模块架构与性能**：整个 Batch 的 Core/IR/Solver/Result/Restart 生命周期；Hot-path 成本、OpenMP/NUMA/CPU P/E 核、GPU Stream/Event/Memory Pool、IO 索引与压缩、数值与数据一致性；至少一个自设计对抗样例。

**Opus 检查数学/数值与可靠性**：中心差分时间层、质量/内力/材料/接触、稳定步长、能量、Patch/制造解；INP 语义及未支持项处理；Restart/Result 损坏、重复历史、时间序列语义。至少 3 个独立反例，能执行则执行。

**统一 Finding 编号**：`Bxx-A-001` / `Bxx-O-001`，合并同根因问题但保留两份独立证据。分类 `C0` 数据损坏/静默物理错误；`C1` 明显数值/性能/契约违约；`C2` 边界或次要可维护风险；`C3` 文档/非阻塞。

**GLM 返工顺序**：C0/C1 → 跨模块接口不一致 → C2（必须决定修复或有期限的 Defer）→ C3；所有修复由 GLM 追加原子提交和回归测试。存在 C0/C1 时禁止稳定合并。

复审策略：若修复仅覆盖已识别风险，审查者只需查看 `BATCH_HEAD_SHA..FIX_HEAD_SHA` 的增量及对应回归；若修复扩大架构边界/重写关键算法，则升级为新的完整综合审核。两位 Reviewer 意见冲突时先独立列证据与可复现最小算例，保守地将该决策列为 BLOCKED，不能只按投票数决定。

合并条件：批次所有必需 Reviewer 报告到齐；C0/C1 全部关闭；测试可复现；性能退化有解释并经批准或回退；旧文件兼容与数值语义通过；未验证项不伪装为通过。合并至 stable 后才开启下一批次；需要连续工作时可先在隔离 worktree 实施不依赖改动的任务。

## 10. 上下文管理与跨会话续跑

建议在仓库中维护以下低成本状态：

```text
docs/development/TASK_POOL.yaml           # 任务依赖/风险/状态/下一个 READY
docs/development/CONTINUOUS_STATUS.md      # 当前批次、上次 PASS、最新 SHA、最近测试
docs/development/DECISIONS.md              # 已确认 ADR/待审核 ADR
docs/development/SESSION_HANDOFF.md        # 下次启动的最小可信上下文
docs/development/BATCH_HISTORY.md          # Bxx 冻结/审查/返工/接受记录
```

每完成一个 Task 更新状态；每完成 2–3 个 Task 执行较广范围回归；每冻结 Batch 编写完整 Review Packet。Codex 会话/上下文接近极限时，不要虚构已自动切换模型或开启新会话，而应先提交代码和状态，生成**可以直接在新 GLM 线程恢复工作的 Prompt**。新线程必须读取真实 Git 与文件，不可仅相信交接文字。

## 11. 不能被“充分利用模型额度”冲淡的边界

- 不能以模型额度剩余为理由引入高风险无测试修改、关闭警告或不合理增大时间步长。
- 不运行危险清理/强推，不擅自下载/上传公司保密模型，不改动生产求解器，不新增未批准的商业依赖。
- 外部 GPU、Linux、Abaqus 许可环境不足时，不使整个项目停止：标记 `BLOCKED_ENV`，切换到独立可测任务；但不得声称相关功能完成。
- 全流程保护模型稳定 ID、Part/Instance 与 SourceMap、材料历史、中心差分时间层、重启动模型指纹、CRC、Results/Restart 版本。
- 未证明加速前不用“超越 Abaqus”；参考模型必须真正比较相同物理、网格、精度、时间步、输出和硬件预算。

## 12. 推荐的实际启动顺序

1. GLM 主线程接收 `02_GLM53_IDLE_AUTOPILOT_PROMPT.md`，读取此计划和 Task Pool，建立 B01 工作分支，连续开发直到 B01 Review Packet 齐全。
2. 用户在需要集中审查的时点，将同一冻结 `BATCH_HEAD_SHA` 分别交 Astra 和 Opus，发送 `03_ASTRA_BATCH_REVIEW_PROMPT.md` 与 `04_OPUS_BATCH_REVIEW_PROMPT.md`；两者相互独立。
3. 两份报告返回后，给 GLM `05_GLM53_REVIEW_FIX_PROMPT.md` 统一处理 Findings。
4. 合并已接受批次后，GLM 按 `06_GLM53_RESUME_PROMPT.md` 开始下一个批次。

**建议的默认节奏**：一个批次积累约 4–8 项可验证任务，审核一次；高风险单元/材料/Schema/接触关键改动可缩短为单任务一批；低风险测试/Bench 可扩大批次。不要按固定天数自动放行未审核代码。
