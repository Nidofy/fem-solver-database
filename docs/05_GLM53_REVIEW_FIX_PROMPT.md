# GLM-5.3：Astra + Opus 批次综合审核后的集中修复 Prompt

你现在从 FEM Solver Database V2 的**连续开发模式**切换到 **Batch Review Repair Mode**。当前 Batch 已冻结并分别接受 GPT-6 Astra 和 Claude Opus 5.5 的独立综合审核。你的唯一首要任务是**消化、复现、修复并回归**审核中发现的问题，不得把返工阶段用于夹带不相关新功能。

## 输入

- 仓库：`https://github.com/Nidofy/fem-solver-database`，以实际本地工作目录为准。
- `BATCH_ID`，`BATCH_BASE_SHA`，`BATCH_HEAD_SHA`：从 `verification/reviews/<BATCH_ID>/manifest.yaml` 读取并验证。
- Astra 报告：`verification/reviews/<BATCH_ID>/astra-batch-review.md`。
- Opus 报告：`verification/reviews/<BATCH_ID>/opus-batch-review.md`。
- 本 Batch 的 `review-packet.md`、测试日志、Benchmark、数值验证、兼容矩阵和开放风险。

禁止只依赖聊天摘要；两份审核报告缺失或指向不同 SHA 时，先报告并进入 `REVIEW_INCOMPLETE`，不要虚构已被双审。

## 1. 先确认源和目标

1. `git status`、当前分支、`git rev-parse HEAD`，校验冻结 BATCH_HEAD 在历史中；禁止在非预期工作树打补丁。
2. 阅读两份审核报告和实际 Diff，生成一份**合并后的 Finding 台账**，保留 `Bxx-A-###` 和 `Bxx-O-###` 原始编号。
3. 识别不同 Reviewer 指向同一根因的重复项，合并修复计划但不抹去独立反例。
4. 对互相冲突的意见，先寻找可重复的数值或源码证据；如果关系到物理行为、公共 ABI、结果文件兼容且无法自行证明，列为 `BLOCKED_DECISION` 并停止相关默认路径扩展。

## 2. 排序修复

优先级：`C0 → C1 → 影响公共契约的 C2 → 其他 C2 → C3`。

逐项按以下闭环处理：

- **REPRODUCED**：复现 Reviewer 提供的输入；无法复现则写明硬件、精度、编译器、实际差异，不可直接判 Reviewer 错误。
- **FIX**：做最小正确修复；对于涉及架构修改的议题，可采用明确版本化或 Feature Flag 保留原路径。
- **TEST**：新增能捕捉原失效模式的回归；数值题必须记录理论参考与容差依据，文件题包含故障注入和旧版读取。
- **REGRESSION**：执行相关模块测试，并跑完整可用 CPU CTest / 数值验证；性能改动保持相同环境前后对比。
- **COMMIT**：一个根因尽可能一个原子修复 Commit，提交消息附 Finding ID。
- **DISPOSITION**：逐项状态使用 `FIXED_VERIFIED / NOT_REPRODUCED_WITH_EVIDENCE / DEFERRED_C2_APPROVED / BLOCKED_ENV / OPEN`；不能私自将 C0/C1 标为被允许延期。

不要仅修改测试阈值、删除失败测试或禁用功能来伪装通过；确需回退错误功能，应明确作为功能回退并更新支持矩阵。

## 3. 增量复审与门槛

修复后记录 `FIX_HEAD_SHA` 和 `BATCH_HEAD_SHA..FIX_HEAD_SHA` Diff，生成 `repair-report.md`，包括 Finding 映射、执行日志、基准、回退/兼容性、剩余风险。

仅针对改动涉及的风险向原 Reviewer 请求**定向增量复审**；若返工超出了 Batch 原约定边界（改文件 Schema、整体重写数值算法、多个核心模块），标注必须重新做完整综合审核。

稳定合并的必要条件：

- 两份独立综合审核均已存在且审查同一冻结 SHA；
- 所有 C0/C1 均 FIXED 且通过复验，或者受影响功能已明确回退/隔离且不存在可达错误路径；
- C2 有逐项处理或经授权的具体 Defer；
- 全量适用测试、独立数值/持久化测试通过；
- 被阻断的 CUDA/MPI/Abaqus 外部环境项目仅登记 `BLOCKED_ENV`，不得变造通过；
- 完整的稳定合并和批次验收动作遵守仓库权限与用户规定。没有授权，不主动推送/合并受保护分支。

## 4. Batch 修复结束后

生成 `verification/reviews/<BATCH_ID>/findings.md`、`repair-report.md` 和 `docs/development/BATCH_HISTORY.md` 更新，输出：

1. BATCH_HEAD / FIX_HEAD / 合并 SHA（若实际存在）；
2. 主要缺陷和根因；
3. 已修复、延后及环境阻断统计；
4. 实际执行的测试与 Benchmark 摘要；
5. 需要复审的增量范围；
6. 是否 `READY_FOR_ACCEPTANCE`，否则明确 `NOT_READY` 原因；
7. 下一批次 READY 任务和必要的 `SESSION_HANDOFF.md`。

**只有在该 Batch 获得明确接受并形成稳定 SHA 后**，才能切回自动开发下一批次；否则继续解决阻断项，而不是用大量新功能掩盖缺陷。

现在开始处理双审 Findings，直接实施修复和测试，不要只写文字上的整改建议。
