# BATCH-<NN> 综合审核交接包模板

> 每个 Batch 使用固定、不可变的源码 SHA。以下占位符由 GLM 从实际 Git 和测试日志填写；不得猜测。

## 1. Batch 身份与范围

- Batch ID：`B<NN>`
- 仓库及真实本地路径：
- 开发分支：
- `BATCH_BASE_SHA`：
- `BATCH_HEAD_SHA`：
- 冻结日期、编译环境：
- 覆盖 Task ID：
- 明确不包含的功能：
- 启用/关闭的 Feature Flags：
- 应审核的所有提交（SHA + 标题）：
- `git diff --stat BASE..HEAD`：

## 2. Task 与跨模块映射

| Task | 代码/接口变更 | 新/修改测试 | 自测结果 | 兼容性影响 | 风险 |
|---|---|---|---|---|---|
| R0.01 | | | | | |

说明受影响的完整生命周期：Input/ModelIR/ModelDB/AnalysisDB/StateDB/ContactDB/Solver/ResultPublisher/ResultReader/Restart/VTK-XDMF/CPU-CUDA。

## 3. 实际测试证据

- 工具链：编译器/版本、CMake、OpenMP、CUDA 和 GPU、操作系统、CPU 拓扑/内存。
- Build/CTest 实际命令、退出码、测试结果总数与失败样例。
- 数值算例：参考公式/输入 Fixture SHA/精度/输出/误差标准。
- ABI/Schema/旧版文件兼容：存档样本、版本矩阵、损坏样本。
- 已尝试但被环境阻断的项目：原因与受影响范围。
- 运行日志路径（非仅截图结论）：

## 4. 性能基准

- Baseline SHA 与改动后 SHA：
- 机器/编译器/Flags/线程、亲和性、模型元素/节点/步数、精度、输出频率、IO 位置：
- Cold-start / Kernel-only / Steady-state / IO-included：
- 中位数/P95/峰值内存/运行次数：
- Raw vs View、OpenMP 扩展、压缩率/查询延迟（适用时）：
- CPU/GPU 物理配置一致性：

## 5. 核心不变量与特别待审事项

- 物理时间层、内力/材料状态刷新不发生隐式 commit；
- 稳定 Node/Element/Instance/Material/Part Identity；
- Result/Restart 指纹、CRC、精度、Schema 兼容与原子写入；
- 单元 Jacobian/能量/时间步及 Contact 缺陷不静默；
- 并行无竞态，CUDA Async happens-before 正确（若涉及）；
- 其他本批特有的不变量。

## 6. Review 工单

**Astra**：使用 `03_ASTRA_BATCH_REVIEW_PROMPT.md` 审核 `BASE_SHA..BATCH_HEAD_SHA` 的完整代码和跨任务调用链，输出 `astra-batch-review.md`。

**Opus 5.5**：使用 `04_OPUS_BATCH_REVIEW_PROMPT.md` 独立审核同一冻结 SHA，输出 `opus-batch-review.md`。初审时两份意见不得互相传递。

## 7. 双审汇总与集中修复（收到报告后填写）

| Finding ID | 来源 | 严重度 | 文件:行号 | 根因组 | GLM 处理 | Fix SHA | 回归测试 | 复审结论 |
|---|---|---|---|---|---|---|---|---|

- `FIX_HEAD_SHA`：
- 差异 `BATCH_HEAD_SHA..FIX_HEAD_SHA`：
- C0/C1 未闭合项：
- C2 延迟批准/期限：
- 独立复审结果：
- 适用全量 CTest/数值/基准结果：
- 是否 `READY_FOR_ACCEPTANCE`：
- 最终稳定分支与 `ACCEPTED_MERGE_SHA`（如果实际完成）：

## 8. Batch 回顾与下一批次

- 真正交付了哪些可运行能力：
- 哪些只是 Parsed/Stored/Prototype/Blocked：
- 哪些性能指标已实测，哪些仍属目标：
- 下批依赖满足的 READY Tasks：
- 续跑 Prompt / 状态文件位置：
