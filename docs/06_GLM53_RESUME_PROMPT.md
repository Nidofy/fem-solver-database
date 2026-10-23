# GLM-5.3 新会话 / 批次续跑 Prompt

继续开发 `https://github.com/Nidofy/fem-solver-database` 的独立 Solver Database V2，作为唯一主实现 Agent。沿用“**多个 Task 自动连续开发 → 冻结 Review Batch → Astra/Opus 双独立综合审核 → GLM 集中修复**”工作制度。

**必须先从本地事实恢复状态，不依赖此 Prompt 猜测当前进度**：

1. `git status --porcelain=v1 -b`、`git rev-parse HEAD`、`git log -10 --oneline`；检测用户未提交变更，绝不清理覆盖。
2. 读取 `AGENTS.md`、`docs/development/{TASK_POOL.yaml,CONTINUOUS_STATUS.md,DECISIONS.md,SESSION_HANDOFF.md,BATCH_HISTORY.md}` 和最近一个 Batch 的 manifest/review/repair 文件。
3. 明确当前状态属于 `IN_DEVELOPMENT / FROZEN_FOR_BATCH_REVIEW / REVIEW_CHANGES_REQUIRED / READY_FOR_ACCEPTANCE / ACCEPTED / BLOCKED` 中哪一种；以代码 SHA 和证据为准。
4. 如果当前有未处理的 Astra/Opus C0/C1 审核意见，先执行 `05_GLM53_REVIEW_FIX_PROMPT.md`，不要启动下一批次。
5. 如果当前 Batch 已冻结但尚无独立审核报告，维护其冻结 SHA，不擅自判定通过；如允许，只做另一个独立 worktree 中不依赖该批次的低风险任务。
6. 如果前一 Batch 已验收，则从真实已接受 SHA 创建下一个开发分支，选择依赖满足的 READY 任务，依据 `02_GLM53_IDLE_AUTOPILOT_PROMPT.md` 连续实现、测试、原子提交、自动取下一个任务。

空闲运行时**继续实施实际代码和验证**，不要每完成一个小任务就停下等待外部 Reviewer；达到 Batch 预算或安全停止条件再生成 Review Packet。

任务完成/切会话前更新 `SESSION_HANDOFF.md`：实际 HEAD、未提交状态、当前 Task、下一任务、最新通过的测试、无法测试的环境、禁止重做的决策和下一条可以直接执行的命令。不能承诺会话结束后自动后台执行。
