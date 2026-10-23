# 从这里开始：连续 GLM 开发 + 双审核包

这个包是根据 2026-10-23 用户提供的 `MODERNIZATION_ROADMAP.md`、上一版 SDB 实施方案和既有源码审查结论调整的 **计划与 Prompt**；**不是实际代码修改或重新执行的 CTest/Benchmark**。

## 使用方式（推荐三次交接）

**第一步，GLM-5.3（连续开发）**

把 `01_CONTINUOUS_DEVELOPMENT_MASTERPLAN.md`、`08_TASK_POOL.yaml` 放入仓库 `docs/development/` 或让 GLM 可以完整读取；向 GLM 主线程发送 `02_GLM53_IDLE_AUTOPILOT_PROMPT.md` 全文。

GLM 在当前可用 Codex 会话中连续实现多个 Task、测试并提交；达到 Batch 截止条件后提供固定 `BATCH_HEAD_SHA` + Review Packet。单个任务不需要等 Astra/Opus。若会话终止、上下文用完，使用 `06_GLM53_RESUME_PROMPT.md` 在新 GLM 会话可靠恢复。

**第二步，Astra 与 Opus（综合审核）**

两位 Reviewer 应审核**完全相同且冻结**的 SHA，而非不断变化的开发分支。分别发送 `03_ASTRA_BATCH_REVIEW_PROMPT.md`、`04_OPUS_BATCH_REVIEW_PROMPT.md`，各自读取完整代码/Diff/测试证据并输出独立报告；第一轮不得先互相看结论。

**第三步，GLM（集中修复）**

将两份报告及固定 SHA 信息提供给 GLM，发送 `05_GLM53_REVIEW_FIX_PROMPT.md`。GLM 归并 Findings、修复、补对抗测试、全量回归、输出 Fix SHA，必要时由对应 Reviewer 仅复审增量。完成 Batch Gate 后方可合并稳定分支，进入下一批。

## 附件清单

- `00_START_HERE.md`：本指南。
- `01_CONTINUOUS_DEVELOPMENT_MASTERPLAN.md`：完整架构/任务调度/Git/验收方案。
- `02_GLM53_IDLE_AUTOPILOT_PROMPT.md`：直接发 GLM 的长运行实施指令。
- `03_ASTRA_BATCH_REVIEW_PROMPT.md`：独立架构与性能批次审核。
- `04_OPUS_BATCH_REVIEW_PROMPT.md`：独立数值/物理/可靠性批次审核。
- `05_GLM53_REVIEW_FIX_PROMPT.md`：GLM 集中处理双审意见。
- `06_GLM53_RESUME_PROMPT.md`：GLM 新会话/批次续跑。
- `07_BATCH_HANDOFF_TEMPLATE.md`：固定 SHA 的审核/返工交接。
- `08_TASK_POOL.yaml`：任务依赖、批次建议、状态与风险。
- `SOURCE_MODERNIZATION_ROADMAP.md`：用户提供的 2026-10-23 技术路线原文。
- `SOURCE_PREVIOUS_EXECUTION_MASTERPLAN.md`：上一版计划，用于对照，不作为批次 Gate 的新规则。

## 重点纪律

- 每项任务自测并原子提交；**每个批次集中外审**。开发中未审核的提交仅能留在开发分支。
- 文件 Schema、材料状态、时间层、Contact identity、CUDA 事件语义发生高风险修改时，缩短批次或隔离实现，不堆积大量依赖。
- 未实际运行的 CUDA/Linux/MPI/商业对照任务只能标环境阻断或未验证。
- Codex/GLM 不能保证会话结束后永久自动运行；通过 Git 与 `SESSION_HANDOFF` 才能可靠长期续做。
