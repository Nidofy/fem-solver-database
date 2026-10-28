# B01 第五轮独立定向复审

日期：2026-10-28（Asia/Shanghai）。实际执行模型：**GPT-6 Astra，reasoning effort High**。本轮由一个独立复审者执行，不是上一轮修复实施者；没有第二位审核者或 Claude/Opus 参与，不将本报告表述为双路通过。

**Verdict: PASS / READY_TO_MERGE。READY_FOR_ACCEPTANCE。**

受审对象为 `15771815c016af66d064e5f4d66dc1dbc6b0a74a`。R4-001/C2（R3-001/A-005 严格键解析残留）与 R4-002/C3（过时交接）均 **CLOSED**。本轮没有新增 B01-R5 finding，没有未关闭阻断项。结论限定于本轮修复门禁；不重新声称物理数值或性能全量验证通过。

## 身份、隔离及范围

我先读取父 AGENTS.md、fem-solver-static-dev 技能与 solution/repo/module maps 的相关部分，再核验开发库 Git 状态、manifest、R4 原报告、findings/repair-report 最新 Round 4、report-provenance 和三份开发台账，另核对 TASK_POOL.yaml。最小边界为批次身份验证工具、控制用例与交接文档。

开发库 `D:/Codes/Solver/ExplicitSolver_track_l_c0a_l0/solver_database_v2` 开始与结束均 clean，分支 `dev/glm-b01`、HEAD 为受审 SHA；origin 为 `https://github.com/Nidofy/fem-solver-database.git`。本机目标分支 `codex/standalone-database` 解析为下面的 BATCH_BASE。本复审未查询远端实时 tip，不将本机引用误称为远端查询。

本人执行 `git clone --shared --no-checkout solver_database_v2 sdb-review-b01-final-r5`，再 detached checkout 到受审 SHA。隔离目录：`D:/Codes/Solver/ExplicitSolver_track_l_c0a_l0/sdb-review-b01-final-r5`。没有修改开发树、修复源码、commit、merge 或 push。下文 `E/` 指隔离根的 `final-r5-evidence/`。

| 身份 | 独立核验 SHA |
|---|---|
| BATCH_BASE | `aaa74afb3c264bbd7ca43555a7445c77994ccaa2` |
| 原 BATCH_HEAD（保持不变） | `719c3fbbe4db1a4dfa0f9d324c3a376f885f47e5` |
| 原 freeze packet | `b58dfff04b484f2ed15524d51883e5f979fd1bc1` |
| R4 受审对象 | `63b24fc14c728af03b1089c864bab95c5bfa8d9b` |
| 本轮修复前置 tip | `294b7c72317a4bc761515eba31692169cd795f81` |
| 解析器修复 | `d01ea18cb3c8d0ce8a6a32fc6849969f9fb2bda0` |
| 本轮受审 tip / 台账修复 | `15771815c016af66d064e5f4d66dc1dbc6b0a74a` |

BASE、BATCH_HEAD、freeze、R4、前置 tip、本轮 tip 六个固定对象通过 commit-object 检查；依次五条祖先检查全部 exit 0。`63b24fc..294b7c7` 仅改变 BATCH_HISTORY 与 CONTINUOUS_STATUS 两份文档；实际修复差异为 `294b7c7..1577181`，共 13 个路径。没有混淆 docs-only 前置提交和本轮 Python 修复。详细命令及输出在 `E/scope.json`、`E/commands.json`。

以下范围门禁均 exit 0 且输出为空：

```text
git diff --name-only 719c3fbbe4db1a4dfa0f9d324c3a376f885f47e5 15771815c016af66d064e5f4d66dc1dbc6b0a74a -- include src tests examples CMakeLists.txt cmake
git diff --name-only 63b24fc14c728af03b1089c864bab95c5bfa8d9b 15771815c016af66d064e5f4d66dc1dbc6b0a74a -- benchmarks
git diff --name-only 63b24fc14c728af03b1089c864bab95c5bfa8d9b 15771815c016af66d064e5f4d66dc1dbc6b0a74a -- schema schemas
git diff --check 294b7c72317a4bc761515eba31692169cd795f81..15771815c016af66d064e5f4d66dc1dbc6b0a74a
```

不存在独立 schema 路径不能单独证明格式不变；格式所在 include/src 已由运行时门禁覆盖。历史 benchmark 修复仍存在，但没有本轮 benchmark 改动，因此不重新打开已闭合计时/表格事项。

## R4-001 / C2 与 R3-001 / A-005：CLOSED

`verification/validate_batch_identity.py:72–75` 明确宣布窄语法：单行键，拒绝双引号键的转义与畸形引号键，包括未知键；单引号反斜杠保持 literal。`:88–95` 对引号开头的列 0 行先校验，双引号正则排除反斜杠；不匹配即 `IDENTITY FAIL: ... unsupported quoted key syntax`。这一步发生在 `:101–102` 的 KNOWN_KEYS 过滤之前，切断 R4 的 unknown-key 静默跳过路径。普通引号键仍去掉包裹引号后进入 `:103–105` 同一重复检测，重复值相同也拒绝。

本修复选择 R4 明确允许的“拒绝不支持格式”分支，没有引入 YAML 库或扩大为完整 YAML 解析器。单引号内 `\u`/`\x` 仍是不同的 literal 键，不应被解码成 identity key；如果唯一 identity 被这样替代，则仍因字段缺失而失败。合法普通引号键、未知元数据键、元数据值中的反斜杠继续按声明处理。

本人在 Python 3.14.0 下执行独立脚本：

```text
python final-r5-evidence/review_checks.py
```

该脚本实际调用正式控制一次，随后重放旧 fixture 和新增控制；没有为增加次数重复执行正式套件。

| 本轮实际测试 | 结果 | 证据 |
|---|---:|---|
| 正式 identity-controls.py | **50/50**，exit 0 | official-controls.log、official-controls-check.json、official-controls-replay.json |
| R4 原始 26 fixture，原 expected_exit 不改 | **26/26** | original-r4-replay.json、复制的原 YAML |
| 本复审者新建边界控制 | **34/34** | independent-controls.json、r5-*.yaml |

正式重放 JSON 与提交记录 **所有字段结构相同**。重放 bytes 为 CRLF 写出，原文件为 LF，故不能称普通 byte/hash 相等：原 SHA-256 `489d4b785281fe4ffe981e80878f2ab102026a72e8ecf1d26743af723acd0579`，重放为 `8f8044326703e0a2e527503ae66222c0b6b7a7d71721623d3ed5433457a0ca46`。脚本使用 try/finally 恢复 clone tracked JSON 原始 bytes，最终 tracked diff 为空。

R4 fixture 来自 `../sdb-review-b01-r4-handoff/reports/merged-r4-evidence/adversarial-identity.json`。逐项用其中 manifest 文件名找到 handoff 目录原件，并验证 **26/26 原始 fixture SHA-256**，逐字节复制到本轮证据目录，再运行当前 validator；没有采用实施者生成的替代 fixture 或修改旧 expected。三个旧 fail-open 分别为 Unicode freeze、hex head、long-Unicode base 重复键，现在均 exit 1，诊断 `unsupported quoted key syntax`。全部调用外部固定 base/head/freeze。

例如从隔离根独立重放旧阻断用例：

```text
python verification/validate_batch_identity.py final-r5-evidence/escaped-unicode-duplicate-freeze.yaml --git-dir . --expect-base aaa74afb3c264bbd7ca43555a7445c77994ccaa2 --expect-head 719c3fbbe4db1a4dfa0f9d324c3a376f885f47e5 --expect-freeze b58dfff04b484f2ed15524d51883e5f979fd1bc1
```

预期/实际均 exit 1，明确不支持引号键语法；另外两项原命令及 stdout/stderr 保存在重放 JSON。旧 26 项中的历史 actual_exit 保留为 previous_actual_exit，避免将历史失败改写成当时通过。

新增 34 项包含：转义双引号位于键两端、双反斜杠、未完成 hex/long-Unicode、尾部孤立反斜杠、未知键未闭合单双引号、引号不配对、闭引号后多余文本、缺失冒号、空引号键及不支持的双单引号格式；转义键置于身份字段之前也必须拒绝。正控制覆盖普通未知单双引号键、单引号 literal 转义、带反斜杠的值、引号键内冒号/另一种引号、注释与嵌套 block 内容。对三个身份字段逐个检查引号键与冒号间 tab 合法、quoted→bare 相同值重复、单双引号混用非法值重复。所有新增项同时断言 exit 与诊断片段，没有以任意异常退出充当通过。

这些控制与源代码顺序分析共同支持关闭本次残留；没有声称支持所有 YAML 的标签、anchor、flow mapping 等语法，也不以未实现完整 YAML 为由新增架构要求。

## R4-002 / C3：CLOSED；历史报告保持不变

SESSION_HANDOFF `:6–30` 已改为 round-4 review/direct repair，区分 R4 受审 SHA、解析器修复 SHA 和后续文档 tip，并要求重新解析 Git 当前 HEAD。原“02e1ee4 后全部 docs-only”陈述已删除，`:20–24` 明确历史修复包括 validator/control 与 benchmark timing；本轮运行时、benchmark 零差异与实际相符。CONTINUOUS_STATUS `:6–24`、BATCH_HISTORY `:5` 与 TASK_POOL `:9` 的 pending/self-verified 状态一致。

受审快照在本报告产生前写“无第五轮独立 review / closure pending”是正确时间状态，不构成阻断。该快照关于尚无 merge/push 授权的陈述记录了当时的候选开发授权边界；本轮用户已明确授权通过后由协调者合并推送，该新指令优先，不能拿旧 pending 或旧授权措辞要求用户再确认。协调者记录本轮验收时应同步推进当前状态及授权记录，历史 FAIL 报告保持原样。

独立读取六份 handoff 原件 bytes，并使用 `git show <固定受审 SHA>:<报告路径>` 的原始 stdout bytes 获取 blob。**12/12 SHA-256** 与 provenance 表相符，**6/6** CRLF→LF 后逐字节相等，详见 `E/provenance-check.json`。R4 原件与 blob 均为 13,337 bytes，SHA-256 均为 `e1d0188401b7b5030fc8c2f9a571799e2af24a30a5450b49f95579b97575e580`，其 CHANGES_REQUESTED / FAIL 未改写。前三轮报告的原件、blob 与归一化规则也保持真实；R3-002 维持 CLOSED。

| 事项 | 本轮处置 |
|---|---|
| B01-R4-001 / C2 | **CLOSED** |
| B01-R3-001 / B01-A-005 残留严格键解析 | **CLOSED**，以上窄语法契约范围 |
| B01-R4-002 / C3 | **CLOSED** |
| R3-002、A-002、A-004、A-006、O-004 | 维持已 CLOSED，不重做旧物理数值/计量审查 |
| 新 B01-R5-### | **无**；未发现可复现的契约相关阻断回归 |

## 验证边界与交接

- **文件变化**：仅隔离根新增本报告与 `final-r5-evidence/`（审核脚本、fixtures、JSON、日志、编号摘录）。正式脚本产生的 tracked JSON 已恢复原始 bytes；开发树 clean、HEAD 未变。父项目文档没有改动。
- **完成内容**：独立固定对象复审、50 正式控制、26 原件重放、34 新边界控制、范围门禁、台账与六份报告原件/blob 溯源。没有把历史 CTest 成绩拼成当前结果。
- **未实施**：没有修复工具、C++、API、算法或 schema，没有提交、合并、推送或开展 B02。
- **未验证**：本轮不编译 C++、不跑 CTest/数值 probes/benchmark、ASan、UBSan、CUDA 或 Linux；没有新增 C++ 变化，因此不需要用这些测试替代本次 Python 门禁。父生产求解器 **not locally compiled / not locally verified**。
- **假设与风险边界**：使用已声明的窄 manifest 单行键格式；调用方继续固定三个 `--expect-*`，generic consistency 本身不能证明预定批次。结论绑定上表受审 SHA；协调者后续若改变解析器或运行时，须重新审查相关差异。
- **迁移与验收**：将本报告及必要轻量证据按原件/blob 双哈希规则记录；由协调者推进验收台账，并按用户已有授权执行稳定分支合并推送。不要把隔离 clone 或审核脚本自动当成生产模块。保持历史 R4 FAIL、CUDA BLOCKED_ENV 与旧验证局限。

**最终结论：PASS / READY_TO_MERGE；READY_FOR_ACCEPTANCE。** 本轮独立复审所需关闭条件已满足，无需另一次修复或额外许可确认。
