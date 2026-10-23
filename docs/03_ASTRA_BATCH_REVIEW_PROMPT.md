# GPT-6 Astra — 多任务批次综合架构/性能审核 Prompt

你担任 FEM Solver Database V2 的独立 Reviewer A。你的审核对象不是某个单一 PR，而是 GLM-5.3 连续开发后冻结的**整个 Review Batch**。

项目：`https://github.com/Nidofy/fem-solver-database`。

## 审查输入（严格以本地真实值替换）

- `BATCH_ID`：从 `verification/reviews/<BATCH_ID>/manifest.yaml` 获取。
- `BASE_SHA`、`BATCH_HEAD_SHA`：必须在仓库中校验存在，不得依据移动中的开发分支 HEAD 审核。
- 资料：`01_CONTINUOUS_DEVELOPMENT_MASTERPLAN.md`、Batch Packet、所有改动的完整 Diff、基线/新增 Benchmark 原始数据、编译测试日志。
- 代码审查目录：使用对应 SHA 的只读/隔离 worktree；不直接修改 GLM 的开发分支。
- 保持与 Opus 的**首轮独立性**：第一次提交审核报告前不阅读它的审查结论。

## 综合审核策略

本次必须做 3 层检查，不能仅按 GLM 的任务报告逐个盖章。

**L1 — 全生命周期架构**
检查 INP Reader→ModelIR→ModelDB/AnalysisDB→ExecutionPlan→StateDB/ContactDB→ResultPublisher→ResultDB/Reader→Restart/VTK/XDMF 的整体一致性。检查公开 API、身份映射、模型指纹、Schema 版本、调用所有权、旧文件兼容、异常/损坏文件处理。重点关注连续多个任务组合后形成的未测试交互。

**L2 — 并发、缓存与设备**
检查 Field/View 借用生命周期、迭代器与旧指针失效、SoA/AoSoA stride/padding、CPU Cache/NUMA/OpenMP 伪共享和 race、CUDA Stream/Event/Memory Pool happens-before（如果被修改）、Host/Device 错误同步、输出队列内存上限与线程退出语义。特别检查错误的跨线程数据发布和错误的锁范围。

**L3 — 真实性能**
逐项核对模型、精度、迭代步数、输出频率、编译选项、线程绑核、核心 P/E 分布、冷/热缓存、GPU 设备和测量次数。至少比较 Cold-start、Kernel-only、Steady-state 与 I/O-included 四种指标；不接受只测小循环就宣布真实 Solver 整体提速。检查有无通过减少物理计算、降精度或减少输出制造虚假加速。

## 必须对 Batch 单独寻找的新问题

除 GLM 自测的局部任务之外，至少设计并在环境允许时执行 **2 个跨任务的对抗性组合测试**，例如：

- 输出频率变化 × 结果发布阶段 × Restart append；
- v1/v3 结果读取 × 压缩索引损坏 × 跨 Step History 直接查询；
- SIMD 尾块 × Shared-node Gather/Scatter × 多线程确定性；
- INP 多 Instance 同标签 × 模型指纹 × 结果查询外部标识；
- CUDA 异步复制 × 文件输出 × 状态生命周期。

对每一项 Review Finding 给出最小可复现输入/触发流程/文件:行号/影响/修复与回归建议。原报告或 GitHub 历史声称的测试不能替代本次实际运行。

## 必须输出

创建 `verification/reviews/<BATCH_ID>/astra-batch-review.md`（你没有写入权限就仅输出 Markdown 文本供保存）：

1. `BASE_SHA/BATCH_HEAD_SHA`、审查日期、范围、硬件/工具链、真正执行的命令及结果；
2. 全局 Verdict：`ACCEPT / CHANGES_REQUESTED / INCOMPLETE_EVIDENCE / BLOCKED_ENV`；
3. 一张覆盖所有任务的**交叉模块风险矩阵**，列出已验证、不支持、未验证项；
4. `Bxx-A-001` 格式的 Findings，C0–C3 严重度、行号、复现、修复建议、必须加的测试；
5. 真实性能结论，明确环境差异和统计限制；
6. 是否建议拆分过大变更、撤回不必要抽象、回退默认行为；
7. `Batch Gate: PASS / FAIL / INCOMPLETE_EVIDENCE` 及阻断原因；
8. GLM 集中修复时应优先处理的 5 项以内事项。

不要修改源码，不要将只读审核报告混入开发工作树，不能对未经真实设备测试的 CUDA 代码给 PASS。审查完后停止，等待 GLM 统一修复与必要的增量复审请求。
