# B01 Reviewer B — GPT-6 Astra 授权代审（数值/物理综合审核）

日期：2026-10-24。**真实执行模型：GPT-6 Astra。用户明确授权以 Astra 替代当前不可用的 Opus。本报告不代表 Claude/Opus 执行结果。** `opus-batch-review.md` 仅保留 04/05 流程兼容文件名。

首轮独立性：本审核只读取本任务输入、冻结包及自身隔离副本，没有读取 Reviewer A 的目录、报告或结论，也没有联系另一 Reviewer。遵循父级 AGENTS.md 与 fem-solver-static-dev；已读父级 solution/repo/module maps。模块边界仅为独立 `solver_database_v2` 的 CPU TET4 lifecycle/result/restart 和 B01 证据，未构建父求解器。

**Verdict: REQUEST_CHANGES（证据/冻结身份修正）。Batch Gate: HOLD。**

实测的 B01 CPU 线弹性 TET4 数值路径未发现失败反例；但冻结 manifest 指向不存在的对象，且 UBSan 覆盖和 OpenMP 效率被误标，不能把整个批次标为 PASS。这里没有提出已证实的 B01 运动方程回归，也不建议仅因这些证据问题回退 lazy publication。

## 1. 冻结身份、范围和执行环境

| 项目 | 独立核验值 |
|---|---|
| BASE | `aaa74afb3c264bbd7ca43555a7445c77994ccaa2`，对象存在 |
| 用户指定内容 HEAD | `719c3fb` → **`719c3fbbe4db1a4dfa0f9d324c3a376f885f47e5`**，对象存在 |
| manifest HEAD | `719c3fb2a5cd3f6d8a8b13eec1b8a7c1d59dd1a4`，`git cat-file -t` exit 128，不存在 |
| 冻结证据包提交 | `b58dfff04b484f2ed15524d51883e5f979fd1bc1` |
| 实际审核 diff | `aaa74afb3c264bbd7ca43555a7445c77994ccaa2..719c3fbbe4db1a4dfa0f9d324c3a376f885f47e5` |
| 隔离仓库 | `D:/Codes/Solver/ExplicitSolver_track_l_c0a_l0/sdb-review-b01-numerical`，detached HEAD |
| 编译 | Windows x64，MinGW g++ 15.2.0，CMake 4.3.1-msvc1，Ninja，Release |
| CTest | SDB_OPENMP=ON，OMP_NUM_THREADS=4；完整独立库 8/8 通过 |
| 独立 probes | g++ `-std=c++20 -O2 -Wall -Wextra -Iinclude`；CPU serial/double |

仓库通过 `git clone --shared --no-checkout solver_database_v2 sdb-review-b01-numerical` 创建，随后 `git checkout --detach 719c3fb`；在副本内 `git restore --source=b58dfff --worktree -- verification/reviews/B01` 复制冻结包。未按移动分支 HEAD 审核。`719c3fb..b58dfff` 仅涉及台账和冻结文档。`git diff --exit-code HEAD -- include src tests benchmarks CMakeLists.txt` exit 0，审核没有修改生产/测试源码。

完整环境与对象命令输出：[reviewer-identity.log](reviewer-identity.log)。本机可能同时运行另一隔离任务，因此不以本次墙钟耗时作性能结论。没有重复百万节点性能计时；冻结 JSONL 是开发方证据，不能冒充审核者实测。

## 2. 源码推导的数学过程与调用链

### 单元、质量、材料

`include/sdb/dynamics.hpp:30–56`：每个 TET4 用常梯度计算 `du_ij = sum_a u_ai N_a,j`；`eps=sym(du)`，存储 tensor shear，顺序 XX/YY/ZZ/XY/YZ/ZX。`sigma=2 mu eps+lambda tr(eps) I`。节点内力为 `volume * sigma * grad(N_a)`；它是抵抗力，因此积分器使用 `Fext+Fcontact-Fint`。

`dynamics.hpp:93–137`：只接受 Tet4、one-IP、material.model==1。正 Jacobian `det>0`，`V=det/6`；三条边的叉积给出梯度，第一节点梯度为其余负和。质量 `rho V/4` 累加到节点；没有一致质量或质量缩放新算法。步长限是基于最大梯度模和纵波速度的保守界。`forces():199–247` 只覆盖内力、应力、应变/局部 scratch 和新增计数，不读取/提交 history。

所以 B01 的“刷新不提交材料 history”对当前线弹性 kernel 成立；不能推及 J2、有限变形或其他有状态本构。C3D8/C3D8R、沙漏、塑性和摩擦/接触算法本批没有改动，本审核没有为这些算法背书。

### 中心差分与结果时间层

`dynamics.hpp:249–277` 初始化：求 `a_0=M^-1(F_0-Ku_0)`，保存 `v_-1/2=v_0-h a_0/2`。`step():280–304`：

```
a_n = M^-1(Fext_n + Fcontact_n - K u_n)
v_(n+1/2) = v_(n-1/2) + h a_n
u_(n+1) = u_n + h v_(n+1/2)
```

一次 advance 后，存储 u 在新时刻，v 在前半步，存储 acceleration/stress/internal_force 通常仍是上一评价层。`lifecycle.hpp:106–118` 在步进前后各施加载荷，末尾时间用 start+increment*dt 重建，因此发布所见 applied force 是当前输出时刻的载荷。

`results/results.hpp:94–103` 先筛 due；只有 due 请求含非 U 变量才求力。`109–114` 重新从力求当前 `a(t)`，输出 `v(t)=v(t-h/2)+h a(t)/2`，不拿陈旧的 StateData.acceleration 冒充当前加速度。RF 在约束分量为 `Fint-Fext-Fcontact`（145、188），其余为零。`ALLKE` 使用同一 full-time V（166–171），`ALLSE=1/2 sum V sigma:eps`（173–178），剪切项乘 2。仅 U 不依赖任何力导出量，因此跳过求力有充分依据。

同一输出时刻的 field/history 以 name/region/history 去重；due 的筛选保留原请求相对顺序。对当前 compiler 白名单（compiler.hpp:392–397），所有可能读取力的变量都被 `name != U` 覆盖。

### 变步长、Restart、材料状态

`lifecycle.hpp:32–60` 的 next_step 在相同物理时刻先用旧载荷/旧 h 还原完整 v，再改变 mask/load，使用新 a 和新 h 重新半步相位化。advance 随后直接推进新步第 1 increment；没有新增单独的 step-start frame，这与严格递增的 writer 时间约束保持原行为。

`save():130–140` 新增求力只刷新派生场，随后 `RestartDB::save` 序列化所有场。它**没有**把存储 acceleration 改为新时刻；因此 checkpoint 是混合存储层，但恢复后的第一 step 在使用 a 前重算力/加速度，物理继续一致。不得宣称 checkpoint 所有场都是同一时层。

`restore():142–162` 校验 cursor/time/config 后恢复 mask/load；`io/restart.hpp:79–85,105–139` 校验 schema/mesh/definition/config、块长度、有限值、CRC 和 mass/inverse。`results.hpp:40–68,202–225,435` 保留 SourceMap 和 model/config identity；field ID 是内部 index+1，实例/原始标签依靠 model 元数据反查，不能当 Abaqus 原始标签直接解读。

### 能量边界

ALLKE/ALLSE 是状态能量，不是完整总能量平衡报告：没有本次新增外力功、接触耗散、沙漏耗散累计量。以下恒力试验独立使用外功 `W=F*u` 检查平衡。中心差分的 full-time 机械能允许有有界 O(h²) 振荡，不能要求每步机械能逐位守恒；当前线性 TET4 无沙漏/塑性/摩擦耗散。

## 3. 独立对抗样例、理论预期和实际结果

全部独立源代码在 [reviewer-probes/probes.cpp](reviewer-probes/probes.cpp) 和 [reviewer-probes/integrity.cpp](reviewer-probes/integrity.cpp)。这些是审核者新增的实验，不是转述开发方验证。`probes.cpp` 的输入来自 frozen forced_tet，但扩展的长时段、步长、载荷反转、输出节奏和仿射矩阵由审核者定义。

### A. 长时振子 + 无输出/U-only/全输出 + 反力/外功

**已运行**。m=1/24，k=100/6，F=1，omega=20；从零初值积分到 t=20（约 64 周期），分别 h=.001/.0005，20000/40000 increments。每步分别执行全变量发布、无输出发布和 U-only 发布。三个 run 的 u/v/a/external/history/time/dt 每个 cursor 严格相等，full snapshot 前后同一 signature 严格相等。

独立解析值 `u=.06(1-cos(20t))`、`v=1.2sin(20t)`。独立离散解 `theta=2 asin(10h)`、`u_n=.06(1-cos(n theta))`，不用求解器的输出作 oracle。full-time V 的离散幅值为 `1.2 sqrt(1-100h²)`。

容差来自相位误差：令 `D=20*abs(theta/h-20)`，取 U 界 `.06D+1e-11`，V 界 `1.2*(D+1-sqrt(1-100h²))+1e-10`；离散解绝对误差上限 1e-10 只覆盖浮点累积。恒力外功 `W=u`，机械能缺陷理论界 `|KE+SE-W|<=3h²`，加 1e-12 roundoff。反力验算 `F+sum RF_x=m*a_free` 容差 1e-12。

| h | 最大 U 解析误差 | 最大 V 解析误差 | 最大离散 U 误差 | 最大机械能缺陷 | 反力平衡残差 |
|---|---:|---:|---:|---:|---:|
| .001 | 3.9740929507886e-4 | 7.9796769502343e-3 | 3.9898639947467e-15 | 2.9999999995173e-6 | 1.1102230246252e-16 |
| .0005 | 9.9353585862119e-5 | 1.9949292255891e-3 | 3.0253577421036e-15 | 7.4999997450725e-7 | 1.1102230246252e-16 |

U/V 最大误差约按 4 倍收敛，与二阶预期一致。full/none/U-only 完成求力计数分别 40002/20001/20001 和 80002/40001/40001，精确符合初始化+步进+due 发布。没有 history commit 或 publication 改变轨迹的反例。

### B. 跨任务：lazy 输出 + 非 due 保存 + 变 dt/载荷反转 + Restart

**已运行**。首步 h=1e-4、t=0.1、F=1，frequency=137；第二步 h=2.5e-4、持续 0.1、F=-0.5，frequency=73。在 increment 733（非 due）保存/恢复，并在首步结束总 increment 1000 再保存/恢复，同步到结束。比较 668 个 cursor 的轨迹和 native packed frame，全部 bit-identical；save 前后轨迹 signature 不变。这同时覆盖 R1.01 的非 due 派生场刷新、R1.02 的发布层、R0.x 的 restart 连续性。

独立连续解：先得到 `u1=.06(1-cos 2)`、`v1=1.2sin 2`；新平衡位移 -0.03，令 tau=t-.1，

```
u=-.03+(u1+.03)cos(20tau)+(v1/20)sin(20tau)
v=-20(u1+.03)sin(20tau)+v1 cos(20tau)
a=-12-400u
```

选择 U/V/A 绝对界 1e-6/1e-5/4e-4：两个子段的离散相位误差分别约 3.33e-7 和 2.08e-6 rad，乘以 O(.13) 位移幅值、O(2.6) 速度幅值，并包含边界半步转换 O(h²) 初值误差；A 界由 400*U 界导出。实际最大误差 U=1.5000548085273e-7、V=6.4258395346961e-6、A=6.0002192339148e-5，全部通过。没有靠增大稳定步长或事后放宽阈值通过。

### C. 含剪切和刚体分量的仿射 Patch

**已运行**。移除约束/载荷，E=100、nu=.25，对同一两 TET 施加 `u=b+G*x`，b=(.05,.10,.15)，

```
G = [.01, .03,-.02;
     .01,-.02, .04;
     .02, .00, .03]
```

理论 tensor strain=(.01,-.02,.03,.02,.02,0)，lambda=mu=40，stress=(1.6,-.8,3.2,1.6,1.6,0)。两单元总体积 1/3，`ALLSE=.042666666666666665`；内力合力及参考坐标力矩为零。包含反对称梯度与平移，可以暴露刚体分量污染应变及工程/张量剪切因子错误。

误差标准是常梯度线性 Patch 的浮点精度：应力/应变 <1e-13，合力/力矩 <1e-14，能量 <1e-15。实际最大 S/E 误差 4.4408920985006e-16，合力/力矩残差 2.2204460492503e-16；能量与理论值同 double 表示。全部通过。

### D. Result/Restart 负向完整性

**已运行**。保存 increment 733 checkpoint；把 E 翻倍再 restore，预期拒绝且目标 state 时间不变。创建含 increment 800 的结果文件，裁掉尾部 3 byte 后默认 reader 必须拒绝。把 checkpoint 的 offset 64 字节翻转一位，restore 必须 CRC 拒绝且 run cursor 保持 800。

实际：分别得到 `restart schema/model/solver/precision/configuration mismatch`、`incomplete result tail (explicit recovery required)`、`native CRC mismatch`；状态未被失败 restore 换入。此处仅验证有限坏块/尾截断和材料身份场景，不等同于所有 I/O 故障、压缩块、并发写入和恢复策略全部验证。

## 4. Findings

严重度定义：C0=灾难性/广泛静默错误；C1=阻断可信冻结或核心能力证据；C2=重要但局部的证据/覆盖缺陷；C3=较小文档偏差。本次没有 C0 或已证实的 B01 数值内核回归。

### B01-O-001 — C1 — manifest 的完整 HEAD 对象不存在

- 文件/行：`verification/reviews/B01/manifest.yaml:11`。
- 最小反例：`git cat-file -t 719c3fb2a5cd3f6d8a8b13eec1b8a7c1d59dd1a4` → exit 128；`git rev-parse 719c3fb` → `719c3fbbe4db1a4dfa0f9d324c3a376f885f47e5`。
- 预期：manifest 提供存在、可 detached checkout、与授权短 SHA 唯一对应的完整 Git 对象。实际：完整 SHA 拼错，自动按 manifest 的 reviewer 无法开始，不能严格复现所声称的 freeze。
- 根因：冻结元数据未以 Git 解析结果核验。用户指定短 SHA 使本次可以继续，但不能修复面向后续消费者的坏 manifest。
- 建议：由 GLM 更正 manifest 和任何引用该假 SHA 的台账；保存一条机器生成的 object/type/diff 断言。不要改变本次审核代码对象。
- 必需回归：新冻结包检查 `cat-file -e SHA^{commit}`，BASE/HEAD 对应既定对象，并断言冻结包后的 commit 不包含代码变化；重新核对 diff。
- Gate：修正前阻断批次整体 PASS。

### B01-O-002 — C2 — “OMP4 效率约 1x”并没有测到 OpenMP 执行路径

- 文件/行：`verification/reviews/B01/open-risks.md:30–31`；`task-evidence/R0.04.md:66–68` 的效率解释；入口 `benchmarks/lifecycle_benchmark.cpp:79`，`include/sdb/lifecycle.hpp:63,84`，`include/sdb/dynamics.hpp:171`。
- 最小反例（源码确定）：benchmark 构造 `CpuRun<double> run(model)`；CpuRun 无 policy 参数，构造 `cpu_(model.mesh,state_)`；CpuExplicit 默认 `CpuPolicy::Serial`，`parallel_for:13` 只有显式 OpenMP policy 才进入 omp parallel for。设置 `OMP_NUM_THREADS=4` 不会改变这条路径。config 字符串自己也写明 serial/deterministic。
- 预期：标为实际 OMP4 的效率结果必须在执行期进入 OpenMP policy，并验证线程路径。实际：两组是同一串行 kernel 在不同环境变量下运行，观察到 1x 不能归因于带宽瓶颈/低并行效率。
- 根因：把编译时 SDB_OPENMP、环境变量与运行时 policy 混为一谈。这不否定 eager/lazy 的串行 A/B 改善，也不证明底层 CpuExplicit OpenMP 有错误。
- 建议：当前批次先改为 serial execution / OMP runtime environment 1 or 4 的准确标签，撤销并行效率诊断；若要测并行性能，应单独设计真正选择 policy 的 benchmark，记录 actual policy/thread count，避免为补报告扩展 CpuRun 公共 API。
- 必需回归：所有 benchmark JSON 标明执行 policy，能以 instrumentation 或明确调用链证明 OpenMP 样本实际调用 parallel region；相同模型/数据/线程条件再比较。该能力目前 **not locally verified**，本审核没有新增性能测量。

### B01-O-003 — C2 — MSVC sanitizer 实际只有 ASan，不能标 ASan+UBSan

- 文件/行：`verification/reviews/B01/manifest.yaml:17`、`build-test.md:33`、`compat-matrix.md:47`、`task-evidence/R0.02.md:32`。根源核验：`CMakeLists.txt:15–21`。
- 最小反例：MSVC 分支仅加入 `/fsanitize=address /Zi` 和 `/INCREMENTAL:NO`；只有非 MSVC 分支才使用 `-fsanitize=address,undefined`。文档使用 cl/MSVC 14.44，未提供任何另行插入 UBSan 的构建命令。
- 预期：矩阵必须区分 ASan 执行与 UBSan 未执行。实际：多个冻结证据文件把仅 ASan 的结果当成 ASan+UBSan 全绿，可能让后续人员错误关闭 UB 验证缺口。
- 根因：从 SDB_SANITIZE 选项描述/跨编译器逻辑推导了不存在的覆盖。
- 建议：更正为 MSVC ASan-only，并显式列 UBSan 未验证；若确需 UBSan 门槛，另用真实支持且可用的 compiler/runtime 执行并保存完整编译参数和日志。不要用本次普通 Release 测试替代 sanitizer。
- 必需回归：核对最终编译命令与加载的 sanitizer runtime；ASan 与 UBSan 分列，没有实际日志就不能记通过。本审核未重新运行 MSVC/ASan，也未声称 UBSan 通过。

### 低优先级勘误（非额外数值 finding）

`numerical-validation.md:24` 写“2002-step run”，源码实际为 2000 increments + initialize + 21 publication = 2022 force evaluations；`:52` 写“1500 post-transition increments”，R1.02 实际是首步 1000、第二步 500，总计 1500。这些不改变已实测数值，但应同步修正文档以免覆盖范围被放大。

## 5. 测试/未测试矩阵与真实日志

| 范围 | 本审核状态 | 证据和边界 |
|---|---|---|
| frozen standalone 完整 Release/OpenMP 构建 | 通过，exit 0 | reviewer-configure.log / reviewer-build.log |
| CTest 所有 8 项 | 8/8，exit 0 | reviewer-ctest.log；完整 stdout 在 reviewer-ctest-full.log |
| CTest lifecycle float/double | 通过（14284 checks） | 属现有测试的独立重新执行 |
| 新增长时解析/离散解、能量、RF、cadence | 通过，double serial | reviewer-probes/run.log |
| 新增变 dt/载荷反转/非 due 与边界 restart | 通过，double serial | 同上，668 packed frame/cursor comparisons |
| 新增含剪切 Patch、刚体分量、合力/力矩 | 通过，double serial | 同上 |
| 新增坏文件/材料身份拒绝 | 通过 | reviewer-probes/integrity-run.log |
| CPU OpenMP 底层已存在 tests | CTest 重新执行通过 | 不代表 CpuRun 以 OpenMP 执行 |
| MSVC/ASan/UBSan | 本审核未执行 | 冻结记录不等同本人运行；UBSan 记录还存在 O-003 |
| CUDA | BLOCKED_ENV / 未编译未运行 | 不存在 GPU 数值/性能通过结论 |
| 输入 Part/Instance、集合、Section、Surface、Amplitude | 定向源码核验 + 现有 lifecycle 测试 | 非整个 Abaqus 子集的穷尽审计 |
| C3D8/R、J2、有限变形、沙漏、摩擦耗散 | 本批未改，未新增验证 | 不能用 schema 存在声称可求解 |
| 新旧 reader、压缩、所有注入故障、生产耦合 | 未完整验证 | 未来相应 batch；不能给工业可用认证 |
| 百万节点/真实 OpenMP 性能 | 未复测 | 原始 JSONL 仅作开发方证据；O-002 必须修正 |

源码门禁补充：`input/abaqus.hpp:215–247,495–497,548–554,597–617` 对未支持参数/物理关键词、NLGEOM、非零边界和输出量拒绝或标为 opaque；`model_compiler/compiler.hpp:69–75,295–297` 能力与拓扑分离，非 Tet4 路径不被默默当作本次线性 CPU 能力。这是受限范围定向核验，不是所有模型身份错误的无缺陷证明。

可重放命令：[reviewer-commands.ps1](reviewer-commands.ps1)。运行工作目录为隔离仓库根。每项实际 exit：configure=0、build=0、ctest=0、probes compile=0、probes run=0、integrity compile=0、integrity run=0。第一次 probes 编译存在两条审核脚本本身的 misleading-indentation 警告；已只改审核脚本排版，最终同参数重编译没有警告，数值未改。完整最终日志保留在上述路径。

## 6. 整体结论、修复顺序和迁移

数学/物理：在本批实际可执行的 CPU 小应变线弹性 TET4 范围，due-first/non-U force gate、full-time V/A/RF/能量重建和 save 派生场刷新有源码依据，独立长时二阶收敛、恒应变 Patch 和跨任务 restart 对抗样例均通过。没有证据要求回退 R1.01；也不能由此批准未来有状态本构复用同一“纯求力”假设。

Result/Restart：已有 schema 没变。独立实测显示非 due save 和变步长边界 restore 后 trajectory/frame 完全一致；材料身份变更、CRC 损坏和尾截断在所测路径正确拒绝。外部调用者观察非 due snapshot 后派生场的语义已变（冻结 compat-matrix 有说明），这是有意契约变化而非“所有行为逐位一致”。本次没有独立重建 BASE 的 pre/post 所有文件 byte equality；该项仍为开发证据和现有回归覆盖，不夸大为审核者全范围实测。

GLM 集中修复顺序：

1. O-001：生成正确冻结 identity，保持 code HEAD 不变，核验所有引用。
2. O-002：纠正 benchmark 执行 policy/效率解释及所有 summary；如保留并行性能目标，明确拆为真实待验证项。
3. O-003：更正 sanitizer 矩阵及 coverage gap；顺带更正两个计数文档错误。
4. 重新冻结证据、重跑对应静态断言/文档一致性检查，再由 reviewer 关闭上述 findings；若引入运行时代码变化，需针对新 diff 复审并重跑对应数值实验。

Files changed：只在本隔离仓库添加本报告、reviewer 日志/命令、reviewer-probes 源码及实验生成文件，并复制 b58dfff 的冻结包。Implemented：独立审核与复现实验。Not implemented：任何生产修复、B02/B02c、公共 API 修改、GLM 消息发送。Assumptions：实际接受的 ModelCompiler CPU TET4 线弹性契约；自然步进中 active nodes 与约束/外载由现有模型路径管理。Migration：将报告和复现实验作为修复输入人工交接；不把隔离 build 或 reviewer scratch 合并为生产源码。父求解器与其外部依赖 **not locally compiled / not locally verified**。

**最终 Batch Gate 仍为 HOLD / REQUEST_CHANGES，待证据身份与覆盖误标闭环；不是数值代码 FAIL，也不是无条件 PASS。**
