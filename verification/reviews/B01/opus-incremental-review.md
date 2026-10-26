# B01 Reviewer B 定向增量复审

日期：2026-10-25，Asia/Shanghai。**真实执行模型：GPT-6 Astra；reasoning effort：high。用户明确授权以 Astra High 代替当前不可用的 Opus。`opus-incremental-review.md` 为流程兼容文件名，不代表 Claude/Opus 执行。**

**INCREMENTAL_REVIEW: PASS**

**Gate 建议：READY_FOR_ACCEPTANCE（仅 Reviewer B 的定向范围；保留一个非阻断 C3 文档发现）。**

B01-O-001、B01-O-002、B01-O-003 均 CLOSED；两个指定计数勘误均 CLOSED。没有新 C0/C1/C2 或数值路径反例。新增 B01-O-004（C3）：原报告副本正文一致，但末行换行归一化改变了原始字节 SHA-256，台账的“verbatim/hash match”说法需准确化。本意见不等于双路复审整体通过，也不授权自动合并。

## 独立性与固定对象

已全文读取 Reviewer B 附件任务、自身首轮 handoff 报告、父 AGENTS.md、`fem-solver-static-dev/SKILL.md`，以及父 solution/repo/module maps 的相关部分。边界为独立库 B01 的证据身份、执行标签、sanitizer 覆盖声明和数值计数勘误。

本审核未读取 Reviewer A 的报告、证据目录或本轮结论，未与 Reviewer A 沟通。按用户授权阅读共享 `findings.md` / `repair-report.md`，只把它们当作开发方待验证声明；其中所引其他 reviewer 的结论没有用作本审核验证依据。所有内容搜索均排除 `astra-review.md`；保留的自身旧报告也从当前声明扫描中排除，另行读取核验。没有要求改写任何原始审查历史。

隔离方式：`git clone --no-hardlinks --no-checkout solver_database_v2 sdb-review-b01-incremental-numerical`，然后 detached checkout FIX_HEAD。开发库 `dev/glm-b01` 在本轮开始及结束均为 `420d5b74c451ad1bfcc41dd873939dbe3c103675`，工作区无改动；不以其后文档替代固定对象。

| 身份 | 独立核验值 |
|---|---|
| BATCH_BASE | `aaa74afb3c264bbd7ca43555a7445c77994ccaa2` |
| BATCH_HEAD | `719c3fbbe4db1a4dfa0f9d324c3a376f885f47e5` |
| 原始 freeze_packet | `b58dfff04b484f2ed15524d51883e5f979fd1bc1` |
| 本次 FIX_HEAD / 隔离 HEAD | `02e1ee40894189ff0274dc6f881273ea7d0ec5a8` |
| 原报告来源 | `../sdb-review-b01-handoff/reports/opus-batch-review.md`（相对隔离库根） |

所有四个对象通过 `git cat-file -e SHA^{commit}`；BASE→BATCH_HEAD、BATCH_HEAD→FIX_HEAD ancestry 均 exit 0。可重放脚本和全部控制输出分别为 [checks.py](reviewer-b-incremental/checks.py)、[checks.log](reviewer-b-incremental/checks.log)。从隔离库根运行：

```powershell
python verification/reviews/B01/reviewer-b-incremental/checks.py
```

脚本最终 exit 0。它只写 reviewer 子目录，不改开发树、生产文件或已冻结证据。

## 数值变更边界：零变更

独立执行 `git diff --exit-code BATCH_HEAD FIX_HEAD -- include src tests examples CMakeLists.txt`：**exit 0，无输出**。额外比较五个 Git tree/blob 身份，全部一致：

| 路径 | 两端共同 Git 对象 |
|---|---|
| include | `d7c42bd099fe72f09253732c3998e97afd088b8e` |
| src | `d3154baa9ca13d8b11a54bf9921340b1355c3c08` |
| tests | `b4874c9ce7c6f27085c6aac32746711faef86ebc` |
| examples | `3ba75db0de3470d68ca210c3586cc1e56092d857` |
| CMakeLists.txt | `b7cae10de995cfdebb1a45111644be3d3dfbdd6a` |

`benchmarks/lifecycle_benchmark.cpp` 的增量已单独阅读：执行标签、额外 setup/kernel 探针、阶段计时、计划/执行/帧计数。它改变 benchmark 测量程序，不改变以上 solver runtime 或 tests。不存在触发“停止定向复审并要求完整数值重审”的运行时代码变化。

据此采用附件允许的边界：本轮不重跑首轮长时振子、Patch、restart probes 或全 CTest。首轮独立数值实验未见反例的结论仍只适用于当时验证的 CPU 小应变线弹性 TET4 路径；没有把本轮静态同一性证据表述成新数值实验通过。

## 三个原 Finding 的关闭证据

### B01-O-001（C1）— CLOSED

`manifest.yaml:14` 已写入 Git 真实完整 BATCH_HEAD；`docs/development/TASK_POOL.yaml` 不再保留假 HEAD 的有效引用。全 tracked 文件扫描（排除 reviewer 原始历史报告）中，完整伪 SHA 只出现在 manifest 明确标为不存在对象的修复史注释；没有继续作为当前身份字段使用。没有要求删除这一历史注释。

亲自运行 `verification/validate_batch_identity.py`，控制结果如下；输出不依赖开发方日志：

| 控制 | 实际结果 |
|---|---|
| FIX_HEAD 的真实 manifest | IDENTITY OK，exit 0 |
| 将内容 HEAD 改回原伪 SHA | 不存在 commit，exit 1 |
| 内容 HEAD 使用七位短 SHA | 非完整 40 hex，exit 1 |
| BASE 改为 40 个 0 | 不存在 commit，exit 1 |
| BASE 改为晚于内容 HEAD 的 FIX_HEAD | ancestry 拒绝，exit 1 |
| freeze 改为 BASE | head→freeze ancestry 拒绝，exit 1 |
| freeze 改为 FIX_HEAD | 检出 benchmark 源文件变化，exit 1 |

最后一个负控制也明确了工具边界：真实 manifest 的 `freeze_packet_sha` 指原始冻结包 b58dfff，不能用其正控制宣称后续 FIX_HEAD 连 benchmark 都没变。本审核另外做了 runtime diff，因此没有混淆两个身份。工具足以验证本次实际 manifest 与指定故障回归；没有声称它是任意 YAML 或所有未来 Git 工作流的完整验证器。

### B01-O-002（C2）— CLOSED

独立源码链仍为 `include/sdb/lifecycle.hpp:62–63` 的 `CpuRun(model)` → 默认构造 `CpuExplicit` → `include/sdb/dynamics.hpp:171` 的 `CpuPolicy::Serial`；`:84` 的 configuration identity 也标 serial/deterministic。新 benchmark `:148` 起逐行输出 `execution_policy:"serial"` 和独立的 OpenMP 编译/环境字段，不能把 `omp_max_threads=4` 当作实际四线程执行。

检查 `task-evidence/R0.04.md:3–17,58–66,95–106`、`open-risks.md:30–41`、`benchmarks/README.md:3–33`：旧并行效率/带宽归因均作为明确撤销的引文保留；当前没有依赖这些 lifecycle 数据的并行效率或带宽结论。eager/lazy A/B 明确限定为串行。README 的低层 OpenMP benchmark 说明属于另一条显式选择 policy 的路径，不是对 lifecycle 的错误复活。

亲自解析八个有效 `lifecycle_*.jsonl` 共 16 行：全部 policy=serial，env1/env4 与 `omp_env`、`omp_max_threads` 一致；全部 plan/executed/frames=21/10/1。用独立脚本重新计算数值中位数，与 `baseline.json` 八行完全一致；原错标 JSONL 位于 `superseded/`，当前汇总不读取它们。

上述为标签/数据一致性验证。本轮没有重跑百万节点计时、没有重新证明性能幅度或 OpenMP parallel launch；源码调用链是本次实际策略判断依据，不使用开发文档所转引的另一 reviewer probe。

### B01-O-003（C2）— CLOSED

独立核对 `CMakeLists.txt:15–22`：MSVC 路径仅 `/fsanitize=address /Zi`，non-MSVC 分支才使用 `address,undefined`。manifest `:21`、build-test `:33–42`、compat-matrix `:47`、R0.02 `:32–39`、R1.01 `:65–66`、CONTINUOUS_STATUS `:33` 均正确限定 MSVC ASan-only；UBSan 显式未执行，缺少 runtime 的理由也已记录。

全 tracked 内容扫描见 [tracked-claim-search.log](reviewer-b-incremental/tracked-claim-search.log)：剩余 `ASan+UBSan` 是明确撤销的旧声明或顶层 README 的“安装相应 runtime 时 GCC/Clang 使用”条件说明，没有发现当前 UBSan 通过声明。旧审核报告不属于现行 sanitizer 矩阵，不要求修改。

本机实际 MinGW g++ 15.2.0 对 `libubsan.a`、`libubsan.dll.a`、`libasan.a`、`libasan.dll.a` 的 `-print-file-name` 查询均只返回未解析名称而不是现有路径，独立支持所用 MinGW 缺少这些 runtime 的原因。没有穷尽扫描整机所有其他 compiler，也没有把“未执行 UBSan”扩大成“验证没有 UB”。本轮未重跑 MSVC ASan；原 ASan 成绩仍是开发方/首轮证据。

## 两处指定勘误 — 均 CLOSED

`numerical-validation.md:24–26` 改为 2000 increments，2022=2000 step + 1 initialize + 21 due publications；与 `tests/lifecycle_tests.cpp:407–415` 的计数断言一致。`numerical-validation.md:52–53` 改为释放后 500 increments，t=0.1→0.2、dt=2e-4；与 `tests/lifecycle_tests.cpp:503–506` 的第二步构造一致。第一步 1000 与第二步 500 没有再被混作 1500 个 post-transition increments。未扩大数值覆盖或修改容差。

## 新增发现

### B01-O-004 — C3 — 首轮报告副本的原始字节哈希声明不准确（非阻断）

位置：`findings.md:3–4,9–12` 的“verbatim copies / hashes verified”和 `repair-report.md:19` 的“hashes match”声明。只验证自己的 B 路报告，未打开 A 报告。

| 实物 | SHA-256 |
|---|---|
| 用户 handoff 原始 `opus-batch-review.md` | `34e234840fc3964081be6e0cb58b1779760ea08971d1c0c2fbcd6c0388d9e191` |
| FIX_HEAD Git blob `verification/reviews/B01/opus-review.md` | `ff74bae84c820ee4376783614951135e0955b826ecb0cbb5f6c250eec3f804c8` |

最小反例：对原文件和 `git show FIX_HEAD:verification/reviews/B01/opus-review.md` 的 bytes 分别求 SHA-256，结果如上。原文件 20609 bytes、仅末行有一个 CR；仓库 blob 20608 bytes、零 CR。将原文件 CRLF→LF 后与仓库 blob **逐字节相等**。所以这是一个换行归一化引起的溯源说明错误，没有正文内容改写或判决变化，也没有损害本次固定代码身份。

建议只修正开发方台账，注明原附件 hash 与 normalized Git copy hash，以及 CRLF→LF 规则；保留原始附件和原始审核结论，不要求改写 reviewer 历史。回归应同时保存原始字节哈希和归一化内容等价断言。

状态：OPEN（C3 文档跟进）；不重开 O-001，不阻断本轮定向 PASS。若后续发现超出换行归一化的内容差异，应重新分级；本次已用字节等价排除了这一情形。

## 验证边界与交接

新增文件仅本隔离库的本报告、`reviewer-b-incremental/checks.py`、`checks.log`、`tracked-claim-search.log` 和六个负控制 manifest。没有修改任何 tracked 生产/测试/benchmark 文件；最终工作树 diff 检查为零。没有提交、推送、合并、B02c 扩展或生产修复。

完成：固定身份与 ancestry、runtime tree/blob 同一性、六个身份负控制、标签/中位数一致性、有效声明扫描、sanitizer 配置和 toolchain runtime 查询、两处勘误与源码计数核对、自己的原报告字节溯源。

未完成且未宣称：本轮完整独立库编译/CTest、数值 probes 重跑、MSVC sanitizer 重跑、UBSan 执行、性能复测、CUDA 编译/运行、父求解器和生产耦合。父求解器仍为 **not locally compiled / not locally verified**；没有新增数值/ABI/schema 风险证据，不能据此认证所有未实现物理能力。

假设：受审对象严格固定于上述 SHA；历史报告和明确撤销引文作为 provenance 保存，而非当前有效声明。迁移：人工把本报告及复现文件交给协调者/GLM，C3 只需台账澄清；B 路三个原问题已关闭，不要求重做数值算法，不把 reviewer scratch 或隔离副本自动合并为生产代码。

**INCREMENTAL_REVIEW: PASS**  
**Gate: READY_FOR_ACCEPTANCE（Reviewer B 定向意见；B01-O-004/C3 非阻断，双路最终 Gate 由协调者/用户汇总）。**
