# B01 第三轮合并定向增量复审

日期：2026-10-26，Asia/Shanghai。**真实执行模型：GPT-6 Astra；reasoning effort：high。**
按用户明确授权，由本审核者单独合并承担原 03 架构/性能与 04 数值/可靠性两路第三轮范围，替代第三轮双路审核；允许阅读两路历史原件。本轮没有第二位 reviewer，也不声称保持两路隔离。母线程仅核验输入、整理交接。

**INCREMENTAL_REVIEW: FAIL**  
**Gate: NOT_READY**

唯一阻断项是 **B01-R3-001（C2）**：身份解析器仍会把嵌套字段/文本块内容当作顶层必填身份，并静默忽略带引号的非法重复身份键。三个 `--expect-*` 均由外部固定时仍可复现 exit 0。原始 `invalid-sha` 反例已修好，但 A-005 的严格解析关闭条件仍未满足。

A-002 外部身份钉死、A-004 计时、A-006 表格与 O-004 指定文件溯源纠正均 **CLOSED**。另有非阻断 **B01-R3-002（C3）**：开发状态文档还保留未限定的 `verbatim` 表述。没有发现或声称新的物理路径错误。

## 1. 固定对象与审核边界

| 身份 | 自行核验值 |
|---|---|
| BATCH_BASE | `aaa74afb3c264bbd7ca43555a7445c77994ccaa2` |
| BATCH_HEAD | `719c3fbbe4db1a4dfa0f9d324c3a376f885f47e5` |
| 原 freeze | `b58dfff04b484f2ed15524d51883e5f979fd1bc1` |
| 第一轮 FIX | `02e1ee40894189ff0274dc6f881273ea7d0ec5a8` |
| 本轮 NEW_FIX / detached HEAD | `d6c67de06b8797ed633a724f7c12f489bf5c2b7a` |
| 开发树观察 HEAD（开始、结束相同） | `3a5f99b84c8f228d96a8af20522af540bf018b38` |

开发库 branch 为 `dev/glm-b01`，origin 为 `https://github.com/Nidofy/fem-solver-database.git`；开始及结束 `git status --porcelain=v1` 均为空。没有 reset、覆盖开发树或使用其移动 HEAD 替代 NEW_FIX。

本人执行 `git clone --shared --no-checkout solver_database_v2 sdb-review-b01-merged-r3`，随后 `git checkout --detach d6c67de06b8797ed633a724f7c12f489bf5c2b7a`。审核目录：`D:/Codes/Solver/ExplicitSolver_track_l_c0a_l0/sdb-review-b01-merged-r3`。

五个固定对象均通过 `git cat-file -e SHA^{commit}`，BASE→HEAD→freeze→FIX→NEW_FIX 祖先检查均 exit 0。实际审核增量为 `02e1ee4..d6c67de`，包含身份 `23b8bc5`、计时 `3c1f2c2`、表格 `b4be2a5`、溯源 `994a607`、台账 `d6c67de`。另外核验 `d6c67de..3a5f99b` 只有 BATCH_HISTORY、CONTINUOUS_STATUS、SESSION_HANDOFF 三个文档，037b809/3a5f99b 没有新增运行时代码。

亲自执行以下门禁，exit 0、输出为空；另对 FIX→NEW_FIX 重复相同路径检查也为空：

```text
git diff --name-only 719c3fbbe4db1a4dfa0f9d324c3a376f885f47e5 d6c67de06b8797ed633a724f7c12f489bf5c2b7a -- include src tests examples CMakeLists.txt cmake
```

因此保持定向范围，无需完整数值重审。新增 benchmark 计时代码单独审查，不冒充零 benchmark 变化。

已读父 AGENTS.md、`fem-solver-static-dev/SKILL.md`、solution/repo/module maps 相关部分；最小边界为独立库的身份工具、benchmark 计时与证据文档，不涉及父求解器。已全文读取两 handoff 的四份历史原件，并读取、核验本库副本及报告台账；副本与原件归一化字节相等见第 5 节。第二轮 A 的 FAIL/NOT_CLOSED 与 B 的限定范围 PASS 均保留，不用本轮意见改写历史。

## 2. 五项逐项处置

以下 `E` 均指本审核库 `verification/reviews/B01/merged-r3-evidence/`。全部验证由本人执行；复现入口为 `python verification/reviews/B01/merged-r3-evidence/checks.py`，最终 **exit 1**，原因是 15 个新增控制中四个应拒绝样例实际被放行；其他已核实项不因此变成失败。

| 要求 | 处置 | 本人命令与证据 |
|---|---|---|
| 1. 身份 A-002/A-005 | **NOT_CLOSED**：A-002 CLOSED；A-005 NOT_CLOSED | 正式 `identity-controls.py` 17/17，JSON 与提交版本结构完全相同；新增控制 11/15 符合，四个 fail-open，见 `E/official-controls-check.json`、`E/adversarial-identity.json`。 |
| 2. 计时 A-004 | **CLOSED** | `git show 3c1f2c2 -- benchmarks/lifecycle_benchmark.cpp verification/reviews/B01/compat-matrix.md`，结合完整源码生命周期检查；见 `E/static-checks.json`、`E/source-lines.txt`。 |
| 3. 表格 A-006 | **CLOSED** | 本人运行 `python verification/reviews/B01/benchmarks/summarize.py --markdown`，独立解析 8 JSONL/16 行，72/72 格与 4/4 差值吻合；见 `E/benchmark-checks.json`、`E/summarize-markdown.log`。 |
| 4. 溯源 O-004 | **CLOSED（指定原件、findings/repair-report）** | 四原件 raw SHA-256、`git show NEW_FIX:...` bytes SHA-256 全部 8/8 匹配表，4/4 归一化逐字节相等；见 `E/report-provenance-check.json`。开发状态文档传播遗漏另列 R3-002。 |
| 5. 范围与台账 | **NOT_CLOSED（仅 A-005 自验关闭声明待更新）** | runtime 门禁通过；历史处置与第二轮 A/B 原意见一致，未偷换为延期或 reviewer 已关闭。`findings.md:143,150–159` 与 `repair-report.md:141` 的全面 FIXED_VERIFIED/严格解析声明仍不能覆盖新增反例。见 `E/scope.json`、`E/static-checks.json`。此项是 R3-001 的台账后果，不重复计一个 C2。 |

其他原编号：A-001/O-002、A-003/O-003、O-001 及两处数值计数勘误维持既有 **CLOSED**。当前有效声明未重新赋予 OMP4 并行效率/带宽结论，MSVC 仍仅 ASan，UBSan/CUDA/Linux/父求解器没有在本轮得到 PASS。本轮没有重新运行或拼接成所谓“双路各 8/8”。

## 3. 身份控制与新增阻断发现

正式运行：

```text
python verification/reviews/B01/task-evidence/identity-controls.py
```

exit 0，17/17 控制符合预期；运行结果与提交的 `identity-controls.json` 全部字段相同。重点结果：

- 真实 detached BATCH_HEAD 的 `freeze_packet_sha: invalid-sha` → **exit 1**，错误直接来自非法字段，已经不依赖偶然的 HEAD diff。
- 缺失/空值/带引号非法值/普通重复键 → exit 1；带引号合法 SHA → exit 0。
- different-valid-head，无 expect → exit 0；带外部 expected head → exit 1。前者已经明确文档化为仅通用一致性，不能充当固定批次证明。
- 脚本 `BASE/HEAD/FREEZE` 是独立固定常量，不从待检 manifest 提取；本人新增 expect-freeze 不匹配也 exit 1。A-002 的期望值断言条件已经满足。

新增边界中，内联注释（当前窄语法不支持）、大写 SHA、大写身份键、空白值、引号空白、未闭合引号均明确拒绝；外层空白、单引号合法值、CRLF 明确接受。没有要求引入完整 YAML 依赖；窄格式可以拒绝不支持的语法，但不能静默成功接受下述结构歧义。

### B01-R3-001 — C2 — 严格身份解析仍把非字段文本当身份，并忽略引号重复键

位置：`verification/validate_batch_identity.py:59–86`，关键为 **68–73 行**。

根因：`line.strip()` 删除了 YAML 层级缩进，再用未限定层级的正则识别身份键；不匹配正则的行又无条件 `continue`。所以文本块中的 `freeze_packet_sha: ...` 被当作真实顶层字段；带引号的同名键不进入 `seen`，绕过重复检测。

最小反例一：正式 manifest 唯一的顶层 freeze 行替换为下面两行，其余不变：

```yaml
notes: |
  freeze_packet_sha: b58dfff04b484f2ed15524d51883e5f979fd1bc1
```

这里 `notes` 的值是文本，顶层 `freeze_packet_sha` 实际缺失。以 `identity_metadata:` 嵌套 map 代替 `notes: |` 也会复现。预期：缺失必填顶层字段，exit 1；实际：**exit 0，IDENTITY OK**。

最小反例二：保留原合法 freeze 行，文末另加：

```yaml
"freeze_packet_sha": invalid-sha
```

预期：拒绝重复身份键或不支持的身份键语法，exit 1；实际：该行被忽略，**exit 0，IDENTITY OK**。单引号键同样复现。三个外部期望值都存在也不能弥补解析器忽略了实际输入的问题。

从审核库根直接重放（把末尾文件名换成 `nested-only-freeze.yaml`、`quoted-duplicate-invalid.yaml` 或 `single-quoted-duplicate-invalid.yaml` 可分别重放另外三个反例）：

```powershell
python verification/validate_batch_identity.py verification/reviews/B01/merged-r3-evidence/scalar-text-only-freeze.yaml --expect-base aaa74afb3c264bbd7ca43555a7445c77994ccaa2 --expect-head 719c3fbbe4db1a4dfa0f9d324c3a376f885f47e5 --expect-freeze b58dfff04b484f2ed15524d51883e5f979fd1bc1
```

四个命令均实际 exit 0，输出同一个合法 base/head/freeze；完整命令与 stdout 在 `E/commands.json`，精简对照在 `E/adversarial-identity.json`。这些是本人新增反例，不是开发日志转述。

影响：机器身份门禁声称验证了不存在的顶层必填字段，或在重复/非法身份输入下给成功结论。真实正式 manifest 本身仍有效；不存在证据表明代码对象已被偷换，也没有数值损坏结论。但“严格解析、缺失与非法不放行”这个本轮明确关闭条件未满足，故 A-005 **NOT_CLOSED**，阻断当前 Gate。

最小修复建议：只允许真实顶层身份字段参与身份映射；保留缩进信息，不把嵌套 map 或 block scalar 内容当顶层字段。对于引号身份键，要么支持其规范化并检测重复，要么明确拒绝；不能归入未知行跳过。继续保持 freeze 必填、无 HEAD fallback、外部期望值。无需修改求解器/API/算法。

回归要求：保留正式 17 控制，再加入本轮四个反例，必须全部 exit 1；增加正常嵌套非身份元数据/文本块不得制造身份值的正负组合，重复键的普通/单双引号拼写都要拒绝。大小写、空白、CRLF、内联注释的接受/拒绝规则应继续明确；若选择窄语法，测试其拒绝分支即可。

## 4. A-004 与 A-006 的关闭证据

`benchmarks/lifecycle_benchmark.cpp:30–34` 的 `timed()` 在回调返回后停止时钟。`:95–98` 先创建空 `std::optional<CpuRun<double>>`，计时回调只执行 `run_storage.emplace(model)`；`:97` 绑定的 run 正是该实例，并继续 initialize/advance。捕获为引用，不会在回调返回时销毁 CpuRun；成功路径的销毁发生在外层 scope 退出，位于计时后。因此原“discarded probe 构造+析构”问题已关闭。

R0.04 `:29–35` 明确记录新字段为实际实例 construction-only、旧轮 probe 包含析构、现行目录为第二轮重跑；旧数据没有混作同定义比较。`compat-matrix.md:36–39` 正确区分计划 21、执行 10、写 1。本人没有编译或重新跑 benchmark；该关闭依据是确定的 C++ 生命周期边界和已提交数据一致性，不声称新性能测量。

独立统计器没有导入或复用 `summarize.py`：逐个解析顶层八个有效 JSONL，全量重算每个数值字段的 median，与提交的 baseline.json 全比，**0 差异**。全部 16 行 policy=serial、plan/executed/frames=21/10/1，env 标签与字段一致，eager/lazy force_evaluations=14/12。

按每个显示值的小数位独立舍入，R0.04 **72/72 格匹配**；从独立 median 重算的四条非 due 差、due-U 差、百分比、steady 对值均一致。实际 `--markdown` 输出的表与 R0.04 表逐行相同，普通 summary 部分与 summary.txt 在换行归一化后相同。输出的 1M bullet 标签为 `1m-`、文档为 `1m`，只是标签尾随连字符，数值无差异，不列实质 finding。

八个 `superseded/*.jsonl` 对原 freeze 同名 blob 的 SHA/bytes **8/8 一致**；当前 glob 非递归，不纳入汇总。显式撤销的 OMP4/带宽引文和历史报告保留合理；当前文档未重新宣称并行效率或整求解器加速。1M 仍只是单样本，未授予统计性能提升结论。

## 5. O-004 的双哈希复核与非阻断残留

按 report-provenance.md 的算法读取原始 bytes，用 `git show NEW_FIX:<path>` 捕获 blob bytes 计算 SHA-256；通过 Python subprocess bytes 避免 PowerShell 文本管道转码。全部八值与表中相应条目相同：

| 报告 | raw bytes / SHA-256 | blob bytes / SHA-256 |
|---|---|---|
| A 首轮 | 21625 / `ab3b58afffb754593b6ae099ebe3a576983a180ce1a61ae868c4bea1125bf6e0` | 21624 / `78763a24e73c58e248fd5b6da65c2060be7841427a24aa5fe9640dc331d61318` |
| B 首轮 | 20609 / `34e234840fc3964081be6e0cb58b1779760ea08971d1c0c2fbcd6c0388d9e191` | 20608 / `ff74bae84c820ee4376783614951135e0955b826ecb0cbb5f6c250eec3f804c8` |
| A 第二轮 | 16761 / `72327ed61378f2b52910c4660498fee057771e4d76293ca72b33e52c9d0347e6` | 16761 / `72327ed61378f2b52910c4660498fee057771e4d76293ca72b33e52c9d0347e6` |
| B 第二轮 | 11448 / `731dd74e41ee335ace9fac867c81b1f670576712cc2e3e22c7d0222574beab22` | 11448 / `731dd74e41ee335ace9fac867c81b1f670576712cc2e3e22c7d0222574beab22` |

首轮各有一个 CRLF，替换 CRLF→LF 后与 blob 逐字节相同；第二轮原件没有 CRLF、直接相同。结论与正文均未改写。findings/repair-report 的当前溯源声明已限定归一化，剩余 `verbatim / hashes match` 是明确承认旧错误的历史引文。因此 O-004 指定范围 **CLOSED**。

### B01-R3-002 — C3 — 开发状态文档未同步归一化溯源措辞（非阻断）

位置：受审 NEW_FIX 的 `docs/development/CONTINUOUS_STATUS.md:18–19`；开发观察 HEAD 3a5f99b 的同一声明位于 `:22–23`。原文仍为 `Both reports recorded verbatim in the packet (see findings.md).`，没有限定 CRLF→LF。这是已有溯源纠正的传播遗漏，不是新的报告正文篡改。

预期：与 findings/report-provenance 一致，说明为 LF 归一化内容等价副本、原始和 blob hashes 分列；实际：两首轮 raw/blob 哈希明确不同，而状态文档仍用未限定 verbatim。重放：

```text
git grep -n verbatim d6c67de06b8797ed633a724f7c12f489bf5c2b7a -- docs/development/CONTINUOUS_STATUS.md
git show 3a5f99b84c8f228d96a8af20522af540bf018b38:docs/development/CONTINUOUS_STATUS.md
```

本人执行的当前文件 `git grep`、后续 HEAD `git show` 与双哈希均保存在 `E/static-checks.json` / `E/report-provenance-check.json`。建议只同步这句有效状态说明并链接 provenance，不改四份历史报告。回归扫描当前台账中与报告副本有关的 verbatim/hash-match 声明，保留标明撤销的历史引用。**单独不阻断 Gate**。

## 6. Gate、变更清单与交接

当前 **FAIL / NOT_READY** 仅由 R3-001/C2 阻断。下一轮最小工作是修正身份解析层并把上述反例纳入控制、更新 A-005 的开发方处置；R3-002 顺手做文档传播修正。已关闭的计时、表格、溯源不需重做百万节点跑数或物理实验，除非后续修改影响其行为或证据。修复后提供新固定 SHA 做窄增量复审，仍由用户人工决定接受及合并。

- **文件变化**：仅本报告与 `merged-r3-evidence/` 新增审核脚本、15 个控制中的 14 个 manifest 文件、命令/静态行号/JSON/日志。正式开发脚本运行会重写本副本 baseline.json、identity-controls.json；已在记录结果后恢复它们的原提交 bytes，最终 tracked diff 为空。开发树始终 clean。
- **实现内容**：独立审核、机器控制和证据复算。没有产品修复、历史报告改写、父文档修改、API/算法/schema 改动、提交、推送、合并或自动发给 GLM。
- **实际验证**：固定 Git 对象/祖先/runtime 空 diff、官方 17 控制、15 个额外控制、8 文件/16 行全字段中位数、72 数值格、4 条差值、8 superseded 原始 bytes、4 报告/8 哈希、有效声明和计时源码检查。
- **未验证**：本轮没有 C++ 编译、CTest、数值 probes、实际 benchmark 复测、MSVC ASan 复跑、UBSan、CUDA、Linux 或父生产求解器验证。benchmark 修改本轮 **not locally compiled**；父求解器 **not locally compiled / not locally verified**。历史/开发方的 8/8 成绩仅是其对应轮次的证据。
- **假设**：受审对象固定于 NEW_FIX；原件取自用户给定 handoff；同一同步线弹性 TET4 运行时契约不变。身份漏洞影响证据门禁，不直接证明物理错误。
- **迁移**：母线程人工交接本报告和轻量证据目录；不要把隔离 clone、临时控制输入或审核脚本当生产修复自动合并。内部生产集成和设备覆盖仍须另行验证。

主要轻量入口：`merged-r3-evidence/checks.py`、`adversarial-identity.json`、四个失败 manifest、`official-controls-check.json`、`benchmark-checks.json`、`report-provenance-check.json`、`superseded-check.json`、`scope.json`、`static-checks.json`、`commands.json`。报告与证据均为本单审核者结果，不组成虚构的第三轮双路 PASS。

**INCREMENTAL_REVIEW: FAIL**  
**Gate: NOT_READY — R3-001/C2 修复并复审前不可接受；R3-002/C3 单独非阻断。**
