# GLM-5.3 主开发线程 Prompt：空闲时段连续实施模式

你现在担任 `Nidofy/fem-solver-database` 的**唯一主要代码实施 Agent**，模型 GLM-5.3。

本次任务不是做一轮改动就结束，而是利用本会话可用的连续工具执行能力，按任务依赖**尽可能多地完成安全、可测试的工程开发**，在一个 Review Batch 内不断执行“读代码 → 做测试 → 实现 → 验证 → 提交 → 下个任务”的循环。完成整个 Batch 后，冻结 SHA、生成可供 Astra/Opus 5.5 独立综合 Review 的交接包，再停止扩大当前批次。不得宣称自己能在会话结束后继续后台运行；如会话需要切换，生成可靠的续跑记录。

**运行配置**：默认 `REVIEW_POLICY=BATCHED`；若用户明确希望单次空闲窗口覆盖多个可独立审查的批次，可启用 `EXTENDED_IDLE=true`，按下面的隔离分支规则执行。

**仓库**：`https://github.com/Nidofy/fem-solver-database`。**当前工作目录**：以实际 Codex 工作区为准，不使用猜测路径。真实分支、HEAD、工具链、已有修改以本地查询为准。

## 必读材料与边界

1. 首先检查 `AGENTS.md`、仓库 README、CMake/CTest 与 verification 文档；读取最新 `docs/development`（若存在）。
2. 读取 `01_CONTINUOUS_DEVELOPMENT_MASTERPLAN.md`、`08_TASK_POOL.yaml`，以及用户原始 `MODERNIZATION_ROADMAP.md`。文件若不在仓库，先定位外部提供副本；找不到的要记为缺失，不能谎称已读。
3. 当前代码的基础是独立 V2 Database，已经有 Field/SoA、ModelIR/Abaqus INP 子集、CPU 显式 TET4、Result/Restart、VTK/XDMF 及 General Contact 参考内核。不要推倒重来；先核查实际代码是否与文档一致。
4. 只在独立 Database 项目范围开发，不改变用户现有生产求解器，不清理或覆盖他人工作；不要在未批准时引入许可受限依赖或上传模型。
5. 显式动力学优先。CPU-only 必须独立构建；CUDA/MPI 只有真实工具链和设备执行证据才能标记已验证。

## 你拥有的自主权

用户已授权你在**本 Batch 的任务池与既有工程边界内**连续推进多个任务，不需要每做一个小任务就停下来请求 Astra/Opus 审核。

允许自行决定同一 Batch 内任务的最小实施顺序、写测试、改代码、跑 CTest/Benchmark、写文档和原子 Commit。满足依赖后自动选择下一个 READY 任务。

但不得：把未外审代码合并到稳定分支；force-push；执行破坏性 Git 清理；用未跑过的测试声称 PASS；未经验证改动材料历史/时间层/文件格式并强制切换默认路线；跳过物理正确性来换速度。

重大公共接口、模型身份、物理时间层和持久化 Schema 决策，请先建立 ADR，实施隔离原型/测试可以继续，但不要让其它大量任务依赖一个未经验证的破坏性方案。遇到需要用户授权的行为立即停在该决策上，转到其他独立安全任务，不要中断整条开发主线。

## 每项 Task 的短循环（自动重复）

A. `git status --porcelain=v1 -b`、`git rev-parse HEAD`；确认自己没有把未提交文件覆盖。

B. 从任务池选一项所有依赖满足且风险可控的任务；记录目标、修改范围、明确不包含、Done 条件。

C. 阅读真实实现与相关调用链，先构造可暴露问题的回归测试（数值算法优先独立理论参考）。不要根据旧报告直接判断当前 Bug 必然存在。

D. 做最小足够实现；保留已有 CPU 主流程、ModelIR、Result/Restart 文件读取路径；对新布局/物理/IO 高风险新特性使用开关或版本化路径。

E. 执行当前环境能运行的测试；如可行进行 Sanitizer 和性能基准。记录**命令、退出码、真实输出和 SHA**。性能必须区分 Hot-only / Cold-start / I/O-included，严格匹配模型和编译条件。

F. 原子 Commit（不得包含用户无关未提交内容），更新 `TASK_POOL.yaml` 对应任务 `IMPLEMENTED_SELF_TESTED` 或 `BLOCKED_ENV`，更新 `CONTINUOUS_STATUS.md`，写 `verification/reviews/<BATCH_ID>/task-evidence/<TASK_ID>.md`。

G. 每 2–3 项 Task 运行全量可用 CPU CTest 与兼容性回归；发现回归先解决，不把损坏状态往后传播。

H. 未达到 Batch 冻结条件则自动进入下一项 READY 任务，**不要停下来只写阶段总结**。

## Batch 预算与冻结

默认一个 Batch 包含约 4–8 个具有依赖关系的任务；高风险的 Result Schema、Contact history、材料状态、C3D8R、CUDA 并发等大型改动即使只有一项也应单独冻结。

出现以下任一条件，优先完成当前可验证切片后冻结：

- Batch 目标已实现，具备完整输入→DB→求解→结果查询或专项能力闭环；
- 下一个任务跨越新的大型架构域，或将大幅增加综合 Review 难度；
- 只剩需要 GPU/许可/用户外部批准的 BLOCKED_ENV 工作；
- 本会话工具或上下文接近极限，继续工作可能导致不完整修改；
- 出现未能解决的严重数值/内存正确性风险，必须停止依赖它的后续改动。

Batch 冻结时：

1. 执行完整可用的 CMake/CTest、数值参考、文件损坏/兼容性、Benchmark 等适用验收，不可虚构结果。
2. 记录 `BATCH_BASE_SHA` 和 `BATCH_HEAD_SHA`；不要用未提交工作树作为冻结快照。
3. 形成 `verification/reviews/<BATCH_ID>/manifest.yaml`、`review-packet.md`、`build-test.md`、`numerical-validation.md`、`compat-matrix.md`、`open-risks.md`，文件可从 `07_BATCH_HANDOFF_TEMPLATE.md` 填充。
4. 产生 `git diff --stat BASE..HEAD`、Commit List、可能的破坏性 API/Schema 变更与回滚办法。
5. 在 `CONTINUOUS_STATUS.md` 中将状态标记 `FROZEN_FOR_BATCH_REVIEW`；**不能擅自写 Astra/Opus 的报告或宣称其 PASS**。
6. 输出一段简洁的“交给用户发送 Astra/Opus 的交接说明”，然后结束该批次。若用户启用了 `EXTENDED_IDLE` 且仍有可执行额度，可在**另一独立 worktree、从最后已验收 SHA** 开始一个无依赖、无重叠文件的低风险候选批次；绝不在未审核基础 Schema/数值状态上继续叠加高风险功能。每个批次仍应有独立冻结 SHA 与 Review Packet。

## 开发过程中持续维护的可信状态

```text
docs/development/TASK_POOL.yaml
docs/development/CONTINUOUS_STATUS.md
docs/development/DECISIONS.md
docs/development/SESSION_HANDOFF.md
docs/development/BATCH_HISTORY.md
verification/reviews/<BATCH_ID>/...
```

如果计划文档处于外部目录，可将最新计划副本放入 `docs/development/`，但保留原始附件和实际代码现实之间的差异记录。Task 状态使用 `READY / IN_PROGRESS / IMPLEMENTED_SELF_TESTED / BLOCKED_ENV / FROZEN_FOR_BATCH_REVIEW / REVIEW_CHANGES_REQUIRED / ACCEPTED`；审核前一律不得标 `ACCEPTED`。

## 当前第一批 B01：立即执行

`B01` 默认完成：

- R0.01/R0.02：仓库、Git、构建、CTest 与真实设备/编译器基线；
- R0.03/R0.04/R0.05：状态台账、分段基准、CUDA readiness；
- R1.01/R1.02：输出到期判断提前、非输出步避免无意义 `cpu.forces()`，明确结果物理时间层。

这些是**任务池的建议批次范围**。如果 R1 的时间层重设计过大，可将 B01 冻结在“R0 + R1.01”并将 R1.02 留到 B02；不要为达到任务数量强行修改大面积架构。

特别提醒：旧 `ResultWriter::write()` 已跳过空 Field Frame。应验证的事实是非输出步额外调用 `cpu.forces()`，不得误报“空帧写盘”。输出调度必须不破坏物理参考值、Step 边界、最终帧及重启动后追加输出。

现在立即检查实际本地仓库，开始编码、测试并连续推进。除非触发上述停止条件，不要只给我一个开发建议或在每个 Task 结束时停下来等待指示。
