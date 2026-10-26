# B01 修复增量独立复审 — Reviewer A（Astra）

日期：2026-10-25，Asia/Shanghai。角色：03 架构 / 性能 / 身份。

**INCREMENTAL_REVIEW: FAIL**  
**Gate: NOT_READY**

原四项处置：**A-001 CLOSED；A-002 NOT_CLOSED；A-003 CLOSED；A-004 NOT_CLOSED**。
新增 **B01-A-005（C2，身份解析失败放行）**、**B01-A-006（C2，现行基准表与原始证据不一致）**。
CPU 回归和所抽查文件兼容均通过；本次拒绝关闭的原因是身份防回归与测量证据口径，不是新发现的物理错误。

## 1. 身份、独立性与边界

自行核验的固定提交：

| 名称 | Git 完整 SHA |
|---|---|
| BATCH_BASE | `aaa74afb3c264bbd7ca43555a7445c77994ccaa2` |
| BATCH_HEAD | `719c3fbbe4db1a4dfa0f9d324c3a376f885f47e5` |
| 原冻结包 | `b58dfff04b484f2ed15524d51883e5f979fd1bc1` |
| FIX_HEAD，本轮检出 | `02e1ee40894189ff0274dc6f881273ea7d0ec5a8` |

原开发树 `solver_database_v2` 的 branch 为 `dev/glm-b01`，origin 为
`https://github.com/Nidofy/fem-solver-database.git`，HEAD 为
`420d5b74c451ad1bfcc41dd873939dbe3c103675`，复核开始/结束 `git status --short` 均为空。
未将其移动 HEAD 代替受审身份。

审核目录为 `D:/Codes/Solver/ExplicitSolver_track_l_c0a_l0/sdb-review-b01-incremental-astra`，
以 `git clone --shared --no-checkout solver_database_v2 sdb-review-b01-incremental-astra`
建立后 `git checkout --detach 02e1ee40894189ff0274dc6f881273ea7d0ec5a8`。
四个对象均为 commit；base→head、head→freeze、freeze→fix 祖先检查均返回 0。

已阅读父 AGENTS、fem-solver-static-dev、三个相关仓库映射、完整 A 定向复审任务及自己的首轮报告。
未读取 Reviewer B 的首轮/本轮报告或结论；增量文件名列表中的历史报告名称仅用于范围登记。
共享 `findings.md` / `repair-report.md` 只当作开发方待验证声明。
原报告与显式撤销的历史表述不作为当前错误声明，也不要求重写历史。

执行：

```text
git diff --name-only 719c3fbbe4db1a4dfa0f9d324c3a376f885f47e5 02e1ee40894189ff0274dc6f881273ea7d0ec5a8 -- include src tests examples CMakeLists.txt cmake
```

退出 0、输出为空。运行时源码、公共接口、默认数值路径及测试源在此区间无变化。
**仍满足定向增量复审边界，无需升级为完整数值/架构重审。** 新增 benchmark 操作在单独可执行文件内。

本报告以下所有相对路径以隔离审核目录为根；证据目录缩写 `E` 指
`verification/reviews/B01/astra-incremental-evidence`。

## 2. 实际执行的独立验证

环境：Windows 10.0.26200，MinGW-w64 GCC 15.2.0、CMake 4.3.1-msvc1、Ninja、
C++20 / Release `-O3 -DNDEBUG`、OpenMP ON，`OMP_NUM_THREADS=4`。
另一个独立审核及正常桌面活动可能并发；未设亲和性/缓存控制，以下计时只描述运行，不支持性能提升声明。

完整配置/构建/测试：

```powershell
$env:PATH='D:/Software/MinGw-w64/x86_64-15.2.0-release-win32-seh-ucrt-rt_v13-rev0/mingw64/bin;' + $env:PATH
$env:OMP_NUM_THREADS='4'
$cmake='C:/Program Files/Microsoft Visual Studio/18/Professional/Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/cmake.exe'
$ctest='C:/Program Files/Microsoft Visual Studio/18/Professional/Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/ctest.exe'
& $cmake -S . -B build-astra-incremental -G Ninja -DCMAKE_BUILD_TYPE=Release -DSDB_OPENMP=ON '-DCMAKE_CXX_COMPILER=D:/Software/MinGw-w64/x86_64-15.2.0-release-win32-seh-ucrt-rt_v13-rev0/mingw64/bin/g++.exe' '-DCMAKE_MAKE_PROGRAM=C:/Program Files/Microsoft Visual Studio/18/Professional/Common7/IDE/CommonExtensions/Microsoft/CMake/Ninja/ninja.exe'
& $cmake --build build-astra-incremental -j 4
& $ctest --test-dir build-astra-incremental --output-on-failure
```

配置/构建退出 0；20/20 build actions；**CTest 8/8 PASS，2.63 秒**。
证据：`E/configure.log`、`E/build.log`、`E/ctest.log`、构建目录的 `Testing/Temporary/LastTest.log`。

修复前后兼容：`python E/compatibility_checks.py` 在 E 内再建独立 detached BATCH_HEAD 副本，
用同 GCC、`-std=gnu++20 -O3 -DNDEBUG -fopenmp` 编译该提交的 `examples/sdb_run.cpp`；
FIX_HEAD 使用本次 CMake 构建的 sdb_run。两侧使用同一 forced_tet.inp（double），运行完整 2000 增量、
保存模型/终态 checkpoint，再运行 `--stop-after 733` 保存 checkpoint 并 `--restart ... --append` 续算。
**5/5 对应文件字节相同；两侧续算文件各自与完整运行结果字节相同。**

| 文件 | 字节数 | 两侧共同 SHA-256 |
|---|---:|---|
| model.sdbmodel | 6444 | `318e4cc5c6d3f21f89f46150666dc979e7078d11a1d1d24de9a76791ba9162d3` |
| full.sdbres / resumed partial.sdbres | 57610 | `75f4c0469b47ef2e172db4892d9965af210e38f2011ef1fc1b0720400c426882` |
| full.sdbrst | 1766 | `3952516e977fc3d8c410752e49a655df54b4bc68e453e2aea1279b85f7ac42a5` |
| stop733.sdbrst | 1766 | `0c5cd8ea4c3dcf102215e346607502a86c3892a5076b4164aac7c9ce26c76ab5` |

命令逐项退出码与结果保存在 `E/compatibility-commands.json`、`E/compatibility-hashes.json`。
初次脚本捕获 Git 文本时遇到 Python 默认 GBK 解码线程警告；未影响 Git checkout、编译、运行或字节比较，
before-source HEAD 另行核验为完整 BATCH_HEAD。脚本随后显式设置 UTF-8/errors=replace，原警告保留在日志；
这属于审核脚本问题，不是产品缺陷。

## 3. 原四项 Finding 处置

### B01-A-001（原 C1）— CLOSED

- `benchmarks/lifecycle_benchmark.cpp:83-86,149-158` 声明的 serial 与 CpuRun 默认 Serial 路径一致。
- `./build-astra-incremental/sdb_lifecycle_benchmark.exe 200000 4096 E/benchmark-output` 退出 0；
  `E/benchmark.jsonl` 实际包含 `execution_policy:"serial", omp_compiled:1, omp_max_threads:4, omp_env:"4"`。
- 自写链接探针 `E/policy_probe.cpp`，以 `g++ -std=gnu++20 -O3 -DNDEBUG -fopenmp -Iinclude E/policy_probe.cpp '-Wl,--wrap=GOMP_parallel' -o E/policy_probe.exe` 编译，
  `E/policy_probe.exe tests/fixtures/abaqus/forced_tet.inp` 退出 0：
  `lifecycle_parallel_launches=0`，`positive_control_parallel_launches=1 visits=16`。
  负控制是真实生命周期调用，正控制是明确选择 OpenMP 的低层 parallel_for；未把环境线程数当执行证据。
- `verification/reviews/B01/benchmarks/README.md:3-34`、`task-evidence/R0.04.md:3-17,97-104`、
  `open-risks.md:29-40` 均明确撤销 OMP4/带宽诊断；真实生命周期并行基准被明确限定为尚不存在。
- `python E/identity_checks.py` 对 `superseded/*.jsonl` 与原 freeze 提交同名 blob 做 SHA-256/字节比较，**8/8 原样保留**，
  `E/superseded-byte-check.json` 可复核。现行汇总 glob 不递归进入 superseded。
- `python E/benchmark_checks.py` 检查新包 16 行及本次 1 行，17 行政策/环境/计数字段均一致。

不将此次低层正控制称作新的 OpenMP 生命周期性能基准；不要求在本次修复增加公共 API。

### B01-A-002（原 C2）— NOT_CLOSED

manifest 的错误完整 SHA **已经修正**，且真实对象/祖先关系全部可解。
在 FIX_HEAD checkout，运行 `python verification/validate_batch_identity.py E/<case>.yaml`：

| case | 实际结果 |
|---|---|
| positive（正式 manifest 原样） | exit 0，IDENTITY OK，base/head/freeze 正确 |
| bad-original-sha（原伪 SHA） | exit 1，does not resolve to a commit |
| abbreviated-head（719c3fb） | exit 1，not a full 40-hex object id |
| base-not-ancestor（base=FIX_HEAD） | exit 1，base is not an ancestor |
| freeze-before-head（freeze=BASE） | exit 1，head is not an ancestor |
| tainted-freeze（freeze=FIX_HEAD） | exit 1，benchmarks/lifecycle_benchmark.cpp 源路径改变 |
| different-valid-head（只把 content head 改为 freeze SHA） | **exit 0，IDENTITY OK，head=b58dfff…** |

完整命令/stdout 在 `E/identity-controls.json`。

最后一项说明此脚本是一般的对象/祖先/端点差异一致性检查，并未实现首轮明确要求的
“compare the expected fixed head”。对于通用 validator，另一个合法祖先不必天然判错；
但 **B01 的预期受审对象是外部固定的 719c3fbbe4db…，此调用未提供/断言它**。
因此不能以此脚本通过等同于“本批固定身份已经被机械锁定”。应由脚本参数或调用层独立固定 expected head/base/freeze，
不应从待检 manifest 自取同一值作为期望。另有新增解析失败放行反例 A-005，说明已出现的 freeze 值校验也不完整。

### B01-A-003（原 C2）— CLOSED

六处当前声明分别在：`build-test.md:33-42`、`manifest.yaml:21`、`compat-matrix.md:47`、
`task-evidence/R0.02.md:32-39`、`task-evidence/R1.01.md:65-66`、`docs/development/CONTINUOUS_STATUS.md:33-34`。
全部已明确 MSVC AddressSanitizer only，UBSan 未执行或链接到明确勘误；CMake 的 MSVC 分支确实仅启用 `/fsanitize=address`。

全仓已跟踪文件检索（排除对方报告内容）：

```text
git grep -n -i -E 'asan.{0,8}ubsan|address.{0,20}undefined|ubsan|AddressSanitizer' -- . ':!verification/reviews/B01/opus-review.md'
```

原始结果在 `E/sanitizer-search.txt`。仍能匹配 `ASan+UBSan` 字面文本，分别是显式错误回顾、自己的历史报告，
以及根 README 对 GCC/Clang 在 runtime 可用时的条件描述；没有发现新的有效“MSVC 已跑 UBSan”声明。
这些历史/条件文本应保留。此关闭只认定覆盖声明已纠正；**没有独立重跑或授予 MSVC/UBSan 验证**。

### B01-A-004（原 C2）— NOT_CLOSED

已经验证的修复：

- 新 benchmark 实跑：`plan_increments=21, executed_increments=10, frames_written=1`，
  `run_setup_ms=15.47, initialize_ms=2.57, kernel_only_ms=1.36`；字段不是仅写在报告里。
- `sdb_run --read E/benchmark-output/million.sdbres` 输出 `frames=1 nodes=200000 elements=50000`。
- kernel-only 包围单次 `CpuExplicit::forces(twin)`；twin StateDB/TetPlan 在计时外构造，
  `kernel_ms` 文档已正确解释成完整 advance。
- `numerical-validation.md:23-26,53-54` 已改成 2000+1+21=2022 次求值及释放段 500 增量。
- R0.04 明确限定为 segmented publication/IO，不宣称完整运行或整求解器加速；不因缺少整跑性能强行扩大本轮范围。

**仍未满足“阶段字段语义准确”的部分：**
`benchmarks/lifecycle_benchmark.cpp:90-94` 把 `CpuRun<double> probe(model)` 放在 timed lambda 的局部作用域。
`timed()` 在调用 `f()` 返回后才停止时钟（同文件 :29-32），所以 `probe` 的析构、StateDB/plan 内存释放也处于计时范围内；
真正将继续 initialize/advance 的 `run(model)` 在第 94 行另行构造，完全不在该计时里。
但 `task-evidence/R0.04.md:29-30` 仍将这个数字称为 `CpuRun construction`。
这是直接可判定的 C++ 生命周期边界问题，不依赖计时噪声；本次 15.47 ms 是构造加销毁的探针，不是当前运行实例的 setup-only 时间。

最小纠正可选择：计时外持有实际 run 的生命周期，仅计时构造；或诚实更名/说明该字段为构造+销毁探针，
并明确实际 run setup 未单独计量。无需改变 CpuRun 公共 API，也不需要数值算法修改。
此外 `compat-matrix.md:36-39` 仍有当前有效的 “now runs 21 increments”，与实测 10 不一致，
属于同一计数口径的 C3 文档残留，应改为“计划 21，实际执行 10”。
新增 A-006 则是独立的现行表格证据同步缺陷。

## 4. 新发现

### B01-A-005 — C2：显式非法 freeze 被解析器当作不存在，检查可失败放行

位置：`verification/validate_batch_identity.py:34-40,59,69-80`。

仅把修复后的正式 manifest 一行改成 `freeze_packet_sha: invalid-sha`。
仍保留正确 BASE 与 BATCH_HEAD，预期应报非法 40-hex。
在本次 E 内独立的真实 BATCH_HEAD checkout 执行（脚本本身来自 FIX_HEAD）：

```text
python verification/validate_batch_identity.py E/malformed-freeze-single-field.yaml --git-dir E/before-source
```

`E/before-source` 的实际 HEAD 是 `719c3fbbe4db1a4dfa0f9d324c3a376f885f47e5`。
实际 **exit 0**，输出：

```text
IDENTITY OK: base=aaa74afb3c264bbd7ca43555a7445c77994ccaa2 head=719c3fbbe4db1a4dfa0f9d324c3a376f885f47e5
```

证据：`E/compatibility-commands.json` 最后一项及 `E/malformed-freeze-single-field.yaml`。
根因：loader 的 regex 只保存已匹配小写十六进制值的行；非法值整行被丢弃，`freeze=None` 后跳过其格式、对象、祖先检查，
转用 HEAD，恰与 BATCH_HEAD 相同，于是空 diff 放行。
这是允许的 `--git-dir` 用法且只改一个字段，不需虚构对象或改产品代码。
在当前 FIX_HEAD 直接用原 head 加同一非法 freeze 会因 benchmark 增量意外被拦住；
那只是 fallback HEAD 恰有源码差异，不能证明非法字段本身被正确拒绝。

建议：区分“字段缺失”和“字段出现但非法”，先保留原始值再严格校验；B01 必需的 freeze 应在调用契约中明确要求。
新增单字段非法/带引号/重复键等解析边界控制，并补上 A-002 所述的外部固定期望身份断言。
不要求为了这个问题换入完整 YAML 依赖，窄范围严格解析也可。

### B01-A-006 — C2：当前 R0.04 基准表不是随包新 JSONL 的统计结果

位置：`verification/reviews/B01/task-evidence/R0.04.md:72-79,87-90`。

执行 `python E/benchmark_checks.py`，先独立从 8 个现行 JSONL 的 16 行重算每个数值字段的 median，
确认 `benchmarks/baseline.json` **与 raw 全部一致**，再按表格每格的小数显示精度比对 R0.04 的 8×9 数值格。
输出 **70/72 格不符**；详见 `E/benchmark-checks.json`。

具体反例：

| tag / 字段 | R0.04 现行表 | 同包 raw 重算 / baseline.json |
|---|---:|---:|
| eager-serial-env1 / kernel_ms | 2.77 | 3.066 |
| lazy-serial-env1 / nondue_snapshot_ms | 0.014 | 0.021 |
| lazy-serial-env4 / snapshot_ms | 1.717 | 2.153 |
| eager-1m-serial-env1 / nondue_snapshot_ms | 8.589 | 7.880 |

这不是比较两次跑数的环境波动：比较对象是 **同一个 FIX_HEAD 包内声称代表相同数据的表与 raw**。
新表没有被标成历史/撤销，而且第 81 行让读者去 baseline.json 查看“exact medians”，因而不能把差异解释成合法历史记录。
表下的绝对值/差值结论也沿用不同数字。现有 `summary.txt` 与当前 raw 一致，说明应同步当前人读报告，
不应覆写 raw 来匹配表。建议从同一统计输出生成表和文本，或明确将旧表归入历史并链接当前结果。
该问题不推翻已确认的 serial 执行政策纠正或 lazy 求值次数减少，但当前 R0.04 数值基线还不能作为可信的统一引用。

## 5. Gate、文件与移交

维持 **INCREMENTAL_REVIEW: FAIL / NOT_READY**，先完成 A-002/A-005 的机械身份防回归、
A-004 的构造阶段边界说明/计量纠正、A-006 的当前表格同步，再做窄增量复核即可。
无需重写历史首轮报告，不要求修改生产数值实现，不要求补做本轮范围外的 OpenMP 生命周期或 CUDA 性能。

- 本次新增：本报告及 `astra-incremental-evidence/` 下独立脚本、探针、日志、隔离参考源/二进制及输出。
- 未实施：任何产品修复、源/API/默认路径修改、父求解器构建、提交、推送、合并；开发工作树始终未改。
- 已验证：固定 Git 对象/祖先、零运行时差异、身份正/负/失败放行控制、完整独立库 CTest、一次 200k benchmark、
  独立并行政策正负控制、5 个前后文件字节兼容和两侧 checkpoint 续算一致性、新 raw/median 和元数据。
- 未验证：MSVC/serial 配置本轮未重新跑，UBSan、CUDA/Linux、父生产求解器未本地验证；没有统计性能提升结论。
- 假设：维持现有同步、linear TET4 生命周期契约；独立库构建可用，不代表父项目外部依赖已经满足。
- 迁移：开发方只需针对上述窄范围修复形成新的固定 FIX_HEAD 与可重放证据；生产集成仍需另行内部平台验证。

建议交付日志/脚本：`identity_checks.py`、`identity-controls.json`、`superseded-byte-check.json`、
`compatibility_checks.py`、`compatibility-commands.json`、`compatibility-hashes.json`、
`benchmark_checks.py`、`benchmark-checks.json`、`benchmark.jsonl`、`benchmark-readback.txt`、
`policy_probe.cpp`、`policy.log`、`configure.log`、`build.log`、`ctest.log`、`sanitizer-search.txt`。
大型参考 checkout、可执行文件及 benchmark 输出可留在独立审核目录，无需作为文本审核包的必要内容。
