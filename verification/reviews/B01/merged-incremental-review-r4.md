# B01 第四轮合并定向增量复审

日期：2026-10-28，Asia/Shanghai。**实际执行模型：gpt-6-astra；reasoning effort：high。** 按用户明确授权，本轮由一个独立审核者合并承担原 A/B 两路的限定范围：身份解析器、控制用例、台账措辞与运行时零差异门禁。本轮没有第二位审核者，也没有 Claude/Opus 参与；不将单人结果描述为双路通过。

**Verdict: CHANGES_REQUESTED / FAIL**

唯一阻断项为 **B01-R4-001 / C2**：双引号身份键中的 YAML 转义未解码，也未作为不支持语法拒绝，导致同名非法重复键仍可被跳过，三个外部期望 SHA 全部固定时仍返回 `IDENTITY OK`。R3 原四个反例已修复，但 R3-001/A-005 的完整严格重复检查关闭条件尚未满足。**R3-002 CLOSED**。另记录一个交接陈述过时的 C3 非阻断项。

## 固定对象与隔离

| 身份 | 独立核验值 |
|---|---|
| BATCH_BASE | `aaa74afb3c264bbd7ca43555a7445c77994ccaa2` |
| 不变的 BATCH_HEAD | `719c3fbbe4db1a4dfa0f9d324c3a376f885f47e5` |
| 原 freeze packet | `b58dfff04b484f2ed15524d51883e5f979fd1bc1` |
| 前轮受审对象 | `d6c67de06b8797ed633a724f7c12f489bf5c2b7a` |
| 本轮受审 NEXT_FIX_HEAD / detached HEAD | `63b24fc14c728af03b1089c864bab95c5bfa8d9b` |
| 开发树观察 HEAD，开始/结束相同 | `294b7c72317a4bc761515eba31692169cd795f81` |

本次用户提示中将 `aaa74af…` 标为 BATCH_HEAD 属于标签笔误；它实际是 BATCH_BASE。审核、命令和外部期望均使用上表，不采用错误标签。五个固定对象通过 `git cat-file -e SHA^{commit}`，BASE→HEAD→freeze→前轮→本轮的四条祖先检查全部 exit 0。

首先执行开发库的 `git status --porcelain=v1 -b`、`git log --oneline -12`，确认 `dev/glm-b01` clean；origin 为 `https://github.com/Nidofy/fem-solver-database.git`。随后本人执行 shared/no-checkout clone，detached checkout 到本轮固定 SHA。隔离目录为 `D:/Codes/Solver/ExplicitSolver_track_l_c0a_l0/sdb-review-b01-merged-r4`。开发树没有被修改；未 commit、push、merge 或修复产品代码。

已读父 AGENTS.md、`fem-solver-static-dev/SKILL.md` 及 solution/repo/module maps 的相关部分；先读 manifest，再读 findings、repair-report 第三轮部分、第三轮报告、report-provenance 和三个 development 台账。最小模块边界为独立库的验证工具与交接文档。以下 `E` 均指隔离库根的 `merged-r4-evidence/`。

实际开发 HEAD 相对本轮固定对象仅改变 `docs/development/BATCH_HISTORY.md` 和 `CONTINUOUS_STATUS.md`，更新第三轮修复和第四轮范围；没有将移动开发 HEAD 偷换成本轮受审对象。证据：`E/scope.json`、`E/commands.json`。

## 运行时范围门禁

本人执行以下命令，exit 0、输出为空：

```text
git diff --name-only 719c3fbbe4db1a4dfa0f9d324c3a376f885f47e5 63b24fc14c728af03b1089c864bab95c5bfa8d9b -- include src tests examples CMakeLists.txt cmake
```

因此允许继续限定增量复审，没有触发 runtime 非空时的 HOLD。另行检查 `benchmarks schema schemas`：BATCH_HEAD→本轮固定对象存在历史 `benchmarks/lifecycle_benchmark.cpp` 修改，不能声称所有源码均未变化；这些来自先前的 `816fb8d`、`d5cad6c`、`3c1f2c2`，属于计量/验证路径。前轮 `d6c67de`→本轮 `63b24fc` 的 benchmark/schema 差异为空。树中没有独立 schema 命名路径，格式相关 include/src 已被 runtime 门禁覆盖。本轮不重新打开 A-004/A-006，也没有重跑其 benchmark。

## 控制重放与处置

本人执行：

```text
python verification/reviews/B01/task-evidence/identity-controls.py
python merged-r4-evidence/checks.py
python merged-r4-evidence/semantic-checks.py
```

第一条由 `checks.py` 实际调用，未为凑次数重复执行。正式控制 **30/30** 符合期望，exit 0；重放 JSON 与提交的 JSON **全部字段结构相同**。Windows 文本写入产生的 CRLF 使重放原始字节哈希与 LF 提交文件不同；已保存重放副本，并以 `try/finally` 恢复 clone 中 tracked JSON 的原始 bytes。最终 tracked diff 为空。

从原 `sdb-review-b01-r3-handoff/reports/merged-r3-evidence/` 读取并逐字节复制四份原始反例，分别运行当前 validator，结果如下。三个 `--expect-*` 全部外部固定。

| 原 R3 fixture | 本轮 exit | 具体诊断 | 处置 |
|---|---:|---|---|
| scalar-text-only-freeze | 1 | missing required identity field(s): freeze_packet_sha | 原反例修复 |
| nested-only-freeze | 1 | missing required identity field(s): freeze_packet_sha | 原反例修复 |
| quoted-duplicate-invalid | 1 | line 52: duplicate identity key 'freeze_packet_sha' | 原反例修复 |
| single-quoted-duplicate-invalid | 1 | line 52: duplicate identity key 'freeze_packet_sha' | 原反例修复 |

另构造 **22 个**额外控制：对 base/head/freeze 各自检查嵌套唯一值、folded scalar 唯一值、引号相同值重复、单双引号混用重复；并检查合法嵌套遮蔽、显式缩进块、全引号键、BOM、tab 缩进和转义键。本组 **19/22** 符合；加上上述四份原件为 **23/26**。三个不符合均属于下一节的同一个根因，不计成三个发现。`checks.py` 最终 exit 1 正是这三个反例触发；`semantic-checks.py` exit 0 表示独立语义确认成功。

| 原发现 | 本轮处置 |
|---|---|
| R3-001 / A-005 | **NOT_CLOSED**：第 0 列与普通引号四例已修复；转义引号键重复仍放行，见 R4-001。不是否认原修复，也不是重计四个旧失败。 |
| R3-002 | **CLOSED**：三台账当前报告副本措辞已限定 LF 归一化与双哈希。 |
| A-002 / A-004 / A-006 / O-004 | 维持前轮 CLOSED；没有新回归证据，不重开旧审。A-002 期望 pinning 控制仍通过，但不能补救解析器跳过输入。 |

证据：`E/official-controls.log`、`official-controls-check.json`、`official-controls-replay.json`、`adversarial-identity.json`、26 个 YAML fixtures、`yaml-key-semantics.json`。

## B01-R4-001 — C2 — 转义后的同名双引号键仍绕过重复检查

位置：`verification/validate_batch_identity.py:84–94`，尤其 `:87` 直接取正则捕获原文，`:93–94` 将未解码键当作未知字段忽略。普通 `"freeze_packet_sha"` 已参与检查，但 `"\u0066reeze_packet_sha"` 并未规范到同一键名。

最小输入，已保存为 `E/escaped-unicode-duplicate-freeze.yaml`：

```yaml
batch_base_sha: aaa74afb3c264bbd7ca43555a7445c77994ccaa2
batch_content_head_sha: 719c3fbbe4db1a4dfa0f9d324c3a376f885f47e5
freeze_packet_sha: b58dfff04b484f2ed15524d51883e5f979fd1bc1
"\u0066reeze_packet_sha": invalid-sha
```

从隔离库根执行如下单行命令即可复现：

```text
python verification/validate_batch_identity.py merged-r4-evidence/escaped-unicode-duplicate-freeze.yaml --git-dir . --expect-base aaa74afb3c264bbd7ca43555a7445c77994ccaa2 --expect-head 719c3fbbe4db1a4dfa0f9d324c3a376f885f47e5 --expect-freeze b58dfff04b484f2ed15524d51883e5f979fd1bc1
```

**预期：exit 1**，拒绝重复 identity key；若项目坚持窄语法，也可以明确拒绝不支持的转义键。**实际：exit 0**，输出：

```text
IDENTITY OK: base=aaa74afb3c264bbd7ca43555a7445c77994ccaa2 head=719c3fbbe4db1a4dfa0f9d324c3a376f885f47e5 freeze=b58dfff04b484f2ed15524d51883e5f979fd1bc1
```

另外两个独立输入仅替换最后一行，结果均为同样 exit 0，期望 exit 1：

| fixture | 末行 | 实际语义键 |
|---|---|---|
| escaped-hex-duplicate-head.yaml | `"batch_content_head_\x73ha": invalid-sha` | batch_content_head_sha |
| escaped-long-unicode-duplicate-base.yaml | `"\U00000062atch_base_sha": invalid-sha` | batch_base_sha |

这不是依赖样式偏好或猜测 YAML 等价性。本机已装 PyYAML 6.0.3，审核脚本调用 `yaml.compose` 保存全部 mapping nodes，直接读出三个样例各自的两条**同名身份键**，末条值均为 `invalid-sha`；没有借助会覆盖重复键的字典加载结果来推断。独立验证命令为 `python merged-r4-evidence/semantic-checks.py`，结果 **3/3**，详情与版本在 `E/yaml-key-semantics.json`。PyYAML 仅用作审核语义参照，未给产品添加依赖。

影响：验证器为含非法重复身份声明的 manifest 出具成功结论，违背 R3 修复声明的“quoted keys join the SAME duplicate-checked namespace”及 A-005 严格解析契约。已固定的 expect 只核对被留下的合法条目，不能发现被跳过的条目。这是批次身份门禁缺陷；没有据此推断求解器物理结果错误。

最小修复方向：在进入 KNOWN_KEYS/seen 前正确规范支持的双引号转义；或者明确限制格式，并对不支持的转义键 fail closed，避免落入 unknown-key 静默放行分支。无需要求完整 YAML 支持或重写架构。将三个反例纳入控制，并保留已有顶层、普通引号、缺失/空值、外部 pinning 正负控制。下一轮应继续只复审 parser/control，不因此重跑数值物理全套。

## 措辞与原件溯源

固定对象中的 `BATCH_HISTORY.md:11`、`CONTINUOUS_STATUS.md:22`、`SESSION_HANDOFF.md:32` 均明确说明 LF-normalized content equality，并分别记录 original-byte 与 Git-blob SHA-256；SESSION_HANDOFF `:20–21` 还明确禁止对 CRLF 原件声称普通 hash equality。开发观察 HEAD 上也保持这三处限定措辞（STATUS 移至 :25）。没有发现三个台账中的有效未限定 verbatim/hash-match 声明。

`findings.md:136`、`report-provenance.md:9` 的旧措辞明确属于承认历史错误的引文；provenance `:36–37` 是有前提的规则，repair-report `:191` 是复审要求，均不误计成当前错误声明。故 **R3-002 CLOSED**。

独立读取五份 handoff 原件 bytes，再以 Python subprocess 获取固定 SHA 的 Git blob bytes，避免 PowerShell 文本管道转码。共 **10/10 SHA-256** 与 report-provenance 相应原件/blob 表项匹配，**5/5** 在 CRLF→LF 后逐字节相等：首轮 A、B 各有一个 CRLF，原件比 blob 各多 1 byte；第二轮 A、B 与 R3 合并报告均无 CRLF，原件 bytes 直接等于 blob。具体完整哈希、长度、路径、逐字节判断均在 `E/report-provenance-check.json`。这次没有改写任何历史报告或其 verdict。

## B01-R4-002 — C3 — SESSION_HANDOFF 仍包含过时的当前状态/范围陈述（非阻断）

位置：`docs/development/SESSION_HANDOFF.md:6–14,27–28,33–35`。当前文件仍说 round-2 repaired / pending round-3；更具体地，`:27–28` 断言 `Any commit after 02e1ee4 on this branch is docs/ledger bookkeeping only.` 这与后续 benchmark 和 identity Python 修复不符。开发观察 HEAD 未修改该文件，因此仍保留同样表述。

复现命令：

```text
git show 294b7c72317a4bc761515eba31692169cd795f81:docs/development/SESSION_HANDOFF.md
git log --oneline 02e1ee40894189ff0274dc6f881273ea7d0ec5a8..63b24fc14c728af03b1089c864bab95c5bfa8d9b -- benchmarks/lifecycle_benchmark.cpp verification/validate_batch_identity.py
```

预期：当前交接范围与实际提交一致。实际：仍指向旧轮次/旧区间，并将实际存在的工具及 benchmark 修改统称为 docs-only。影响是新会话可能从过时范围起步；开头已要求重新核验 Git，且 CONTINUOUS_STATUS 的开发 HEAD 已更新，所以此项**单独不阻断 gate**。建议随下一次交接刷新轮次、SHA 和变更类型，不作为恢复数值重审的理由，也不重新打开已关闭的 hash wording 问题。证据：`E/wording-check.json`、`scope.json` 及编号源码摘录。

## 验证边界与交接

- **文件变化**：仅隔离目录新增本报告及 `merged-r4-evidence/` 脚本、fixtures、JSON、命令日志/编号源码摘录；正式控制运行生成的 tracked JSON 已恢复原始 bytes，隔离库最终 tracked diff 为空，开发库 clean 且 HEAD 未动。
- **实施内容**：独立审查、30 正式控制、4 原始反例重放、22 额外控制、3 次独立转义语义确认、范围/措辞/5 报告双哈希检查。
- **未实施**：没有修复产品、改 API/算法/schema、提交/推送/合并、向开发者自动发送消息、改变父项目文档或历史结论。
- **未验证**：本轮没有 C++ 编译、CTest、数值 probes、benchmark 实测、MSVC ASan、UBSan、CUDA 或 Linux 复测；父生产求解器 **not locally compiled / not locally verified**。历史 8/8 不属于本轮成绩，不与本轮 Python 控制拼接成新回归结果。
- **假设**：按已声明的窄 manifest 格式与“普通/引号身份键统一重复检查”契约评判；允许明确拒绝不支持格式，不要求支持全部 YAML。以固定 NEXT_FIX_HEAD 为受审对象，开发 HEAD 仅做差异核对。
- **迁移**：人工交接报告及轻量证据；不要把隔离 clone 或审核脚本自动并入生产。修复 R4-001 后提供新固定 SHA，并做相同 parser/control 窄复审；R4-002 可随交接更新。内部生产依赖与设备覆盖仍属于独立验证事项。

**最终结论：CHANGES_REQUESTED / FAIL。** 解除条件为 R4-001 的同名转义键重复不再被放行，新增回归控制通过并经固定 SHA 复审；R4-002 为 C3 非阻断，R3-002 已关闭。
