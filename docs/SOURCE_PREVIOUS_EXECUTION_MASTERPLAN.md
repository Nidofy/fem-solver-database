# FEM Solver Database V2：GLM-5.3 持续开发与 Astra / Opus 5.5 双重审核总计划

**版本**：Draft 1.0 · 2026-10-23  
**目标仓库**：https://github.com/Nidofy/fem-solver-database  
**公开分支（审查时显示）**：`codex/standalone-database`；开始实施时必须重新执行 Git 查询，以实际工作树 SHA 为准。  
**输入基线**：用户提供的《MODERNIZATION_ROADMAP.md》；GitHub README、`verification/README.md` 以及指定 C++ 源文件的公开版本。  
**状态说明**：本文件是实施方案，不是执行/验证结果；GitHub 公布的过往 8/8 CTest 是历史记录，非本次重新运行。

## A. 总体策略与项目边界

- **GLM-5.3** 是唯一的日常代码实现负责人：以单一需求台账持续交付小批次可测试补丁，维护任务状态、阶段提交、回归证据。
- **GPT-6 Astra** 独立审查架构边界、性能、缓存/线程/CPU-GPU 同步、并行调度、文件布局和横向兼容。
- **Claude Opus 5.5** 独立审查有限元理论、物理数值正确性、时间积分/能量、材料与接触算法、输入语义、数据损坏恢复及对抗测试。
- Astra 与 Opus 的审查应先相互隔离，各自提交证据，然后由 GLM 修复、测试并形成统一处置表。双审的重点是不同视角，**不是两个模型轮流说“通过”**。
- 不允许三个线程对同一个 Git 工作树同时写入代码；审查者原则上只读对应 Commit/PR，不直接修改工作分支。
- 全部工作首先局限于**独立 Database 仓库**。不得修改原生产求解器或其 CMake/内部私有依赖；生产接入通过独立 adapter 和后续单独批准的集成 PR。
- 保留现有 MeshDB、StateDB、GeneralContact、ModelIR、`.sdbmodel`、`.sdbres`、`.sdbrst` 和 ABI 语义。任何不兼容的文件格式或 API 修改都必须版本化、迁移并经专项审核。
- 严格区分 `可解析 (Parsed)`、`可表达 (Stored)`、`可计算 (Executable)`、`已验证 (Verified)` 和 `可用于生产 (Qualified)` 五种能力，不允许降级或无依据宣称与 Abaqus 功能或性能等价。

## B. 已确认资产与优先缺口

| 类别 | 公开源码/附件线索 | 当前判断与后续动作 |
|---|---|---|
| 核心 DB | `include/sdb/{field,model,state,contact,implicit}.hpp` | SoA/AoSoA、模型冻结、材料/状态/接触基础可复用。AoSoA 有布局但尚未证明真实批内核受益。 |
| 生命周期 | `include/sdb/{input/abaqus.hpp,model_ir/model_ir.hpp,lifecycle.hpp}` | 已具备 Abaqus 子集→IR→CPU TET4→结果/重启；应按垂直物理功能扩展，而非重复搭建解析框架。 |
| 结果库 | `include/sdb/results/{results,export}.hpp`, `io/native.hpp` | 有 Field/History、CRC、读写、VTK/XDMF；缺流式压缩、直接历史索引、异步有界队列和 out-of-core。`.sdbres` 不是 HDF5；不能擅自把 `.sdbres` 改名为 `.h5`。 |
| 输出路径 | `examples/sdb_run.cpp` 每次 `advance()` 后 publish；`ResultPublisher::publish()` 开始先 `cpu.forces()` | **首要修正候选**：在快照前判断是否到期，非输出增量不重算内力；当前 `ResultWriter::write()` 已过滤空 Field Frame，**没有空帧落盘问题**，但仍存在多余的 forces/快照调用；须对照旧语义与结果精度。 |
| 结果读取 | `ResultReader` 构造时完整扫描/解码，历史查询遍历每帧 | 适合小规模验收，不适合上万帧单节点历史曲线；先增加持久索引+选择性读取，再引入压缩。 |
| CPU 核心 | `dynamics.hpp` 中 `parallel_for` 使用 `schedule(static)`；TET4 标量 Kernel | 先剖析并行调度与 P/E 核、View/raw，再做 SIMD/AoSoA、缓存排序。 |
| 参考物理 | `CpuExplicit` 当前小变形线弹性 TET4 | 新单元先做 C3D8 全积分，随后 C3D8R+沙漏；不要用几何形状相同替代物理 formulation。 |
| 接触 | `general_contact/module.hpp` 与 `search.hpp` | 有 CPU NTS/边缘基础和 BVH；大规模性能、跨面历史、CCD、mortars 仍待专项验证。 |
| CUDA | `src/cuda_backend.cu`、`cuda_memory.hpp` | 代码存在，但公开验收材料明确未本地编译/设备验证，GPU 不得标 PASS。 |
| 结果/重启 | `verification/README.md` | 记录 Windows 8/8 CTest 与多项契约，但不能代替 Linux/GPU/MPI 或生产对照。 |
| 性能记录 | 附件 2026-10-22 测量 | 8T float 效率约 32%、View/raw ≈1.36、输出/端到端占比问题；先用新基准重复确认。 |

**需要主动纠偏的两条路线图表述**：
1. “单核耗时 ≥ Abaqus”方向相反或含糊。统一使用 `Speedup = Abaqus_elapsed / SDB_elapsed`；大于 1 才表示同条件下 SDB 更快；对照未形成前不得写“超越”。
2. “对标 500+ 单元族”属于多年期产品路线，不属于短期数据基础设施交付条件。用明确的**物理能力/模型输入/数值检验矩阵**取代粗糙的数量百分比。

## C. 任务依赖与分层

```text
R0 复现基线 / CI / 工具链
 ├─ R1 输出调度与同步正确性 ──► R4 ResultDB v3（索引/压缩/流式）
 ├─ R2 CPU 性能诊断 ──► R3 SIMD、AoSoA、NUMA、缓存分块
 │                             ├─► R5 实体单元垂直片（C3D8→C3D8R）
 │                             └─► R7 接触持久搜索 / 并行装配
 ├─ G0 CUDA Toolchain + TET4 设备验证 ──► G1 单卡完整求解链
 │                                          └─► G2 GPU 接触/多卡（需分区服务）
 └─ R5 单元/材料/工况语义 ──► R6 非线性材料与 NLGEOM ──► R8 Shell/Beam
                                             ├─► R9 MPI/Partition
                                             └─► R10 Implicit Solver Adapter
```

**两条快车道**：R0 后可让结果库 R4 与 CPU 性能 R2 在两个不同代码域开展；G0 工具链验收可以与 R1/R2 并行，但 G1 不得在真实 CUDA 设备验证之前宣布完成。

## D. 分波次开发路线与明确出口

### R0：基线冻结、验证与任务台账（强制第一阶段）

- `R0.01` 读取 `AGENTS.md`（如存在）、实际目录、`git status/branch/log`，记录 `BASE_SHA`、工具链及 CPU/GPU/NUMA/内存配置；留存公开仓库与本地 roadmap 不一致的清单。
- `R0.02` 复现 serial/OpenMP、MSVC/Clang/GCC 可用构建；跑既有 8 项测试及 independent-process 输入-模型-结果-恢复用例。每项给真实命令与输出。缺少环境则标 `BLOCKED_ENV`。
- `R0.03` 建立 `docs/development/{TASKS.md,STATUS.md,DECISIONS.md,BASELINES.md}` 与 `verification/reviews/`；任务状态只能取 `PLANNED/READY/IN_PROGRESS/IN_REVIEW/BLOCKED/ACCEPTED`。
- `R0.04` 固定模型与基准口径，分别测 model-read、compile、finalize、TetPlan、force、assemble、integrate、publish、serialize、IO、restart、reader-open、single-history-query；给 P50/P95、峰值内存、模型尺寸、编译器和线程亲和信息。
- `R0.05` GPU 验证基础设施：检测 NVCC/host compiler/GPU、分设 compile 与 device-test 工程阶段；设备不可用时登记 `not locally verified`，不要改 CMake 让测试假通过。

**出口**：每个现有能力可复现；有不带硬件猜测的性能 JSON；新代码分支和台账就位。R0 不能提前重写 Kernel。

### R1：计算—输出解耦、物理时刻一致性（高优先级）

- `R1.01` 将 OutputScheduler 的 `due` 判断移到 snapshot/force 刷新之前，只有确有请求到期才创建 Field/History Frame；区分仅需 U 的快照与需更新 S/E/RF/能量的快照，避免无条件刷新内力；不可改变频率、末帧和跨 Step 边界的语义。
- `R1.02` 将输出刷新与材料状态更新隔离；定义 `t[n]`、`v[n-1/2]`、应力/反力等的快照时刻契约。当前 TET4 允许纯弹性刷新，但非线性材料输出禁止隐含 commit/历史迭代。
- `R1.03` Writer 引入有界队列接口和 Backpressure 策略：`Block`、显式错误中止；不能静默丢弃用户要求的结果。首期先串行 IO/可测试，随后可选后台线程。
- `R1.04` 失败注入：磁盘写失败、跳帧、重复 Frame、异常 Step 切换、Restart append；状态不得被结果写入失败污染。

**出口**：非输出步无额外结果重算/快照构造；同输出语义下旧新文件与历史曲线一致（旧 Writer 本来就会跳过空 Frame），正常/分段重启均能通过独立进程检验。**R1 完成后再宣称“纯求解热循环”性能。**

### R2：CPU 诊断、线程拓扑与零成本访问

- `R2.01` 提供可控 `OMP_PLACES/OMP_PROC_BIND` 或平台线程绑定参数及拓扑诊断；在 Ultra 9 285H 上分别测试 P-core、E-core、混合线程集合。不可仅凭猜测把 8T 回退归咎于 P/E 核。
- `R2.02` 分离 OMP 热点：节点更新、单元 Gather、应力/力、归约、装配、状态提交；记录调度/负载不平衡/内存带宽瓶颈。
- `R2.03` 比较 Static、Guided/Task、Thread-local scratch 与确定性归约，保留默认数值确定性协议；只对有意义的负载启用动态调度。
- `R2.04` 对比 raw contiguous、`View`、`CheckedView`（检查视图仅在调试/边界层）与编译器汇编/优化报告，定位约 1.36 的 View/raw 比值来源并验证改进。
- `R2.05` `finalize`/`TetPlan` 优化：并行生成邻接、单元几何、材料分组，确保浮点合并顺序和模型指纹稳定；拆开冷启动与 1000/10000 步稳态的测量。

**出口**：以实测替代假设；不存在有统计意义的 8T 反向退化（或给出硬件上限证据）；raw/View 访问开销向 ≤1.05 目标靠拢，未达标要有原因。不得为了看起来更快关闭安全/确定性测试。

### R3：SIMD、AoSoA、缓存块与精度

- `R3.01` Formula/Batch/Scheduler 三层接口，纯标量 TET4 算法作为 golden oracle；`std::experimental::simd` 或 Intrinsics 必须经编译器支持检测，不能将非标准 API 无条件设为 C++20 公共契约。
- `R3.02` 为 TET4 建立 4/8/16 lane（按真实 ISA）AoSoA Batch，布局转换只在 Model Finalize，区分批内占用与转换成本。
- `R3.03` 试验 Morton/RCM/图分块/按邻接 tile 重排的效益和内存开销，保留外部 ID 与 SourceMap 的稳定映射。
- `R3.04` 精度策略从 float/double 全类型扩展到**可选**精度契约；首先保持完全相同的原有精度参考，混合精度仅在误差预算及长期能量漂移测试后开启。
- `R3.05` NUMA first-touch/线程局部 scratch、可选大页；优先构建实测收益后加入默认；不强迫桌面机使用服务器专属策略。

**出口**：可关闭的 SIMD/分块选项 + 功能回归 + 多机/多编译器对照 + 高/低阶硬件回退路径。性能目标按 R0 基准重估，不用单一微基准替代真实求解流程。

### R4：ResultDB v3 与长期持久化（可与 R2/R3 并行）

- `R4.01` 引入版本化结果记录，保留 v1 `.sdbres` 只读/升级路径；保持 `IResultStorage`/`ResultReader` 上层 API 的合理稳定。
- `R4.02` 按 Frame/Field/Region/Time 建立可修复的磁盘索引与 footer/sidecar，完成随机单帧读取和实体 History 通道直接寻址；索引损坏可安全重建。
- `R4.03` 可选 zstd 无损分块压缩和未压缩块混用；提供字段级压缩配置、CRC、源精度及实际存储精度。不得默认对 double 进行有损量化。
- `R4.04` 读路径流式/out-of-core，避免打开文件就逐帧完整反序列化；给出内存预算与边界测试。用户自定义 Query 支持跨 Step、单实体/集合、IP、Contact、Global。
- `R4.05` 有界异步输出流水：Snapshot Staging → Compression → Durable Writer；GPU 情况等待相应 Event 后拷贝；队列阻塞不能隐式丢帧。
- `R4.06` 文件一致性：损坏压缩块、截断、异常结束、重复附加、旧版解析、schema 变更、跨精度模型指纹错配；恢复只能修复已知可恢复的尾部，不能跳过内部数据损坏。
- `R4.07` XDMF/VTK 高效导出；进一步 HDF5 可作为单独可选 Backend 的预研，不得把原生 `.sdbres` 宣称为 `.h5`，尤其 Windows SWMR 需实测文件系统语义。

**出口**：十万帧级元数据/小型 history 压测可按显存/内存预算配置；打开耗时与查询耗时相对全扫显著改善；结果与重启严格绑定；损坏注入测试与 v1 向后兼容通过。

### R5：工业实体单元的首个垂直物理切片（C3D8 → C3D8R）

- `R5.01` 将 `ElementDescriptor` 从纯 ModelIR 语义提升为求解器可执行 descriptor：拓扑、形函数、积分、局部面、旋转/位移自由度、截面、材料及状态 schema；保留旧 model 序列化兼容。
- `R5.02` `C3D8` 全积分线弹性小变形：形函数/雅可比/质量/内力/稳定步长/应力输出；先 CPU Serial，再 OpenMP/SIMD。
- `R5.03` `C3D8R` 缩减积分 + 受控沙漏稳定化及沙漏能量监控；**严禁将缩减积分单元视作普通 HEX8**。
- `R5.04` 针对两种单元完善 Abaqus INP 语义、Section、DLOAD/BOUNDARY/CLOAD、输出位置/积分点、SourceMap；`Parsed/Stored/Executable/Verified` 分别标注。
- `R5.05` 测试：线性位移/恒应变 patch、刚体运动零内力、静力反力平衡、能量守恒/单调性、hourglass 零能模、收敛曲线、负 Jacobian、错误节点顺序。

**出口**：从真实 `*.inp` 导入至 ResultReader/VTK 的 `C3D8/C3D8R` 端到端计算，float/double 交叉验收，并有独立数值参考。此后才与 Abaqus 同模型比较。

### R6：材料、载荷与 NLGEOM

- `R6.01` 材料 Schema Registry：参数维度/单位、历史布局、trial/commit、版本迁移及结果 Field 注册；CPU 插件 API 不应直接假装可在 CUDA device 运行。
- `R6.02` J2/Mises 率无关塑性 + return mapping + 收敛/卸载验证；然后针对显式需求选择 Johnson–Cook 率效应/损伤或超弹，不同时推多种材料。
- `R6.03` 有限变形应变/应力更新、客观性/刚体转动试验、当前构形 Jacobian/随动压力；与小变形 Kernel 明确隔离。
- `R6.04` DLOAD、MPC/CEQ、Tie、接触属性、Amplitude 的 IR→编译→运行通路。每个关键词具备官方语义核验及“未支持即拒绝”测试。
- `R6.05` 质量缩放、体积黏性、稳定步长和可选子循环；任何扩大 dt 的策略必须有稳定性/能量验证，不以增加 dt 本身作为成功标志。

**出口**：至少一个弹塑性实体冲击模型完成闭环验证、Step/Restart/ResultHistory 审计通过；新增变量正确存储并可查询。

### R7：General Contact CPU 现代化

- `R7.01` 为 NTS 全面引入可保留跨步状态的 BVH/Refit/周期性 rebuild 策略；与每步重建版本公平比较准确性、内存和时间。
- `R7.02` 面片候选生成并行化、批量追加、排序去重和确定性装配；对百万候选附近规模进行预算和负载偏斜测试。
- `R7.03` 几何连续性/特征边、边-边和跨面摩擦历史迁移；离散化从现有 NTS 扩展罚式 STS，然后决定 mortar，不要同时上全套。
- `R7.04` CCD 时刻约束的全域集成，不能以已有 triangle TOI primitive 声称完成全域接触自动 cutback。
- `R7.05` 接触能量、穿透、能量耗散、摩擦客观性、重复接触历史/分离再接触测试；结果库加入 Kontakt/Contact 位置字段。

**出口**：由真实板件撞击/滑移等有限元参考模型支撑数值/性能结论；接触 CPU 性能改善有分段证据，维持失败可回退到旧搜索策略。

### G0/G1：CUDA 从“源码存在”转为“确实跑过”

- `G0.01` Windows CUDA+MSVC 或 Linux NVCC CI/独立设备矩阵；对 TET4 CPU/CUDA float/double、Mirror/Stream/Event、内存池、生存期、非法访问和并发顺序执行实际 GPU 测试。
- `G1.01` GPU 全流程 resident state：包含时变荷载/约束/接触力更新、能量归约、选择性下载和输出；禁止每增量隐式 Host staging。
- `G1.02` C3D8/C3D8R GPU 公式及装配策略（atomic/分段归约/确定性模式），对照 CPU 分布误差；同步/占用/拷贝分别测量。
- `G1.03` 先单卡接触搜索/力更新闭环，再升级 GPU 性能优化；GPU 多卡必须在 Partition/Owned-Ghost 契约稳定之后。

**出口**：GPU 设备名、驱动、编译器、执行命令、真实数值和耗时记录可核验。没有 GPU，就只能登记 `G0/G1 BLOCKED_ENV`，不得宣称实现完整。

### R8：壳梁、高阶及工程功能（长期逐片交付）

候选顺序：`S4R (壳厚度/转动/减缩积分/截面点)` → `B31 (截面方向/弯扭耦合)` → `TET10` → `C3D20R` → Cohesive → 其他多物理场。每一族都严格按 `Descriptor → Material/Section/DOF/State → Kernel → INP → ResultDB → Reference Tests → CPU/GPU` 交付；没有完整闭环不算完成该族。

### R9：MPI/多 GPU 与分布式数据（长期）

先定义 Owned/Ghost/Global-ID/Partition 指纹、Halo 生命周期及 Domain Decomposition；随后 MPI 节点力/场交换、稳定时间步/能量归约，再接跨分区接触与分布式 Result/Restart。单机缓存布局不能因 MPI 注记而出现热点分支。跨 rank 数逐位确定性是研究目标，现实验收先设清晰容差与固定拓扑内可重复性。

### R10：隐式与独立求解器后端（长期）

StateDB 增量 Trial/Rollback，DofMap/CSR/BSR、约束消元、符号分解缓存，后接可替换 MUMPS/PARDISO 接口、Newton line-search、隐式时间积分、特征值。直接解法不应写死为唯一方案，也不能让隐式辅助状态强制占据显式热路径。

### R11：跨格式导入与产品接入（长期）

在 ModelIR/Capability Report 基础上逐步加入 Nastran BDF、LS-DYNA Keyword、Gmsh 等 Reader，每个 Reader 先实现有验证的子集；区分单位/坐标/集合/刚体/约束语义差异。与生产 Solver 通过 standalone adapter 接入，遵守旧求解器原仓库不可修改的边界，必要时新开授权的集成项目。

## E. 建议首轮任务清单（可直接排期）

**迭代工作量估算（不是交付时间承诺）**：以“一个可审查的 PR + 自动化回归 + 审核返工”为一个开发单元，R0 约 3–5 单元、R1 约 3–4、R2/R3 约 6–10、R4 约 5–8、R5 首个实体单元闭环约 5–8；后续接触/材料/CUDA/MPI/隐式按研究难度滚动拆分，不给不可信的日历截止日期。

| 排序 | 任务 ID | 实施内容 | 依赖 | 任务级证据 | 默认复审 |
|---:|---|---|---|---|---|
| 1 | R0.01 | 冻结 SHA、工作树及代码现实清单 | — | BASELINE.md, SHA | Astra |
| 2 | R0.02 | CI 与 8 个现有 CTest 回归 | R0.01 | 跨编译器日志 | GLM 自审 |
| 3 | R0.04 | 细分 CPU/Result 性能基准，JSON schema | R0.02 | 可复现实测 | Astra |
| 4 | R0.05 | CUDA Toolchain/Device Gate | R0.02 | 编译与设备信息 | Astra |
| 5 | R1.01 | OutputScheduler 提前、避免非输出步无意义 forces | R0.02 | 分段/非输出步测试 | Astra + Opus |
| 6 | R1.02 | 明确 Result 时间层与状态无副作用 | R1.01 | 中心差分物理检验 | Opus |
| 7 | R1.04 | Restart/Result 故障注入与完整性 | R1.02 | 失败后重启/查询 | Opus |
| 8 | R2.01 | CPU 拓扑、P/E core affinity benchmark | R0.04 | 分核型 JSON | Astra |
| 9 | R2.04 | View/raw 访问层对照 | R0.04 | 汇编/计时和测试 | Astra |
| 10 | R2.05 | TetPlan/finalize 并行及长跑摊销测量 | R2.01 | 初始化/稳态分开 | Astra |
| 11 | R4.01 | `.sdbres` v3 版本与 v1 读取 | R1.04 | 兼容矩阵 | Astra + Opus |
| 12 | R4.02 | 稀疏帧索引、History 直读 | R4.01 | 大帧数随机访问 | Astra |
| 13 | R4.03 | zstd/CRC/崩溃恢复 | R4.01 | 文件故障注入 | Astra + Opus |
| 14 | R3.01 | Batch 接口、SIMD golden test | R2.04 | 标量/SIMD 对照 | Astra + Opus |
| 15 | R3.02 | TET4 AoSoA / ISA 回退 | R3.01 | 多精度和布局回归 | Astra |
| 16 | R5.01 | ElementDescriptor/Section 计算契约 | R0.02 | ModelIR 兼容 | Astra + Opus |
| 17 | R5.02 | C3D8 全积分闭环 | R5.01 | Patch/能量/INP/Result | Astra + Opus |
| 18 | R5.03 | C3D8R 与沙漏控制 | R5.02 | 零能模/沙漏能量 | Astra + Opus |
| 19 | G0.01 | CUDA TET4 真设备验收 | R0.05 | GPU logs + numeric | Astra + Opus |
| 20 | R6.01 | Material/History Schema | R5.01 | restart/compat schema | Astra + Opus |
| 21 | R6.02 | J2 塑性纵向完整切片 | R6.01 | return mapping/卸载 | Opus |
| 22 | R7.01 | 持久 BVH 重建/更新择优 | R0.04 | 对照 search 事件日志 | Astra + Opus |

**任务大小原则**：单项 PR 尽量是一项可独立复现的能力，一般不同时修改 `field.hpp`、数值物理和大文件 IO 三层。规模过大须继续切分。专注 `R0 → R1 → R2` 后，再启 `R4` 与 `R5` 两条工作流。

## F. 审核制度、Git 隔离与工作协议

### 1. 分工与审批

| 变更类别 | GLM-5.3 | Astra | Opus 5.5 | 必须通过的硬门槛 |
|---|---|---|---|---|
| 文档/非行为改动 | 开发+自审 | 按需 | — | 构建/一致性 |
| CPU 性能/并行/内存 | 开发 | 必审 | 关键路径必审 | 数据竞争/性能/确定性 |
| 物理算法/材料/接触 | 开发 | 重要变更必审 | 必审 | 分析解/patch/能量/稳定性 |
| Input/ModelIR/ABI | 开发 | 必审 | 必审 | 守恒的语义映射/反例 |
| Result/Restart/Schema | 开发 | 必审 | 必审 | version/完整性/向后兼容 |
| CUDA/MPI | 开发 | 必审 | 必审 | 真设备/真通信环境证据 |
| 仅测试补充 | 开发 | 关键测试按需 | 关键测试按需 | 测试会红、能抓真实回归 |

### 2. Git 原则

- 工作基线：`BASE_SHA` 必须来自当前实际分支，不假定 `main` 存在。
- GLM 每任务创建 `feat/sdb-<ID>-<short>`，在自己的 worktree 实施。所有审查只针对 `BASE_SHA..HEAD_SHA` 的冻结 Diff。
- Astra 和 Opus 的审查彼此独立且只读；要求写出文件/行号/复现命令/失效输入。代码不是证据的单句赞美不算审核。
- GLM 逐条处理 Finding，生成 `Disposition.md`：`FIXED / REJECTED_WITH_EVIDENCE / DEFERRED_WITH_REASON`；不得无解释删除审查意见。
- 修复后只审核增量 Diff + 原始关键风险点；再次通过全量测试和性能回归，更新状态为 `ACCEPTED`。重要接口改动先双审设计，再写实现。
- 保护主干：未通过硬门槛的实验性 SIMD/GPU/MPI 放在 feature flag 或独立分支，默认路径持续可用。

### 3. Findings 等级

- **C0 Blocker**：数据损坏、未定义行为、隐式计算错误、错误物理结果且没有错误提示、文件持久化破坏；禁止合并。
- **C1 Major**：高概率数值偏差、显著性能退化、确定性违约、重大契约/API 破坏；修复或实验性功能隔离并重审。
- **C2 Moderate**：边界案例、可维护性、次要性能问题；记录具体修复任务或在同 PR 修复。
- **C3 Minor**：文档/命名/样式；可批量处理。
- **Environment Blocked**：缺 NVCC、GPU、MPI 集群或商业许可；不能被 C2 降级掩盖，报告必须列出 not verified 范围。

### 4. 每个任务的完成定义（DoD）

1. 功能有明确最小可运行路径；没有 Stub 代替验收范围。
2. 现有 tests + 本任务新增红绿对照测试通过；失败或缺环境如实记录。
3. 提供编译器、优化选项、硬件、数据规模、精度、线程亲和及执行次数信息。
4. 参考解/patch/制造解或已知对照验证（涉及物理）；float/double 误差预算合理。
5. 访问层、内存、Result/Restart 未引入不可解释的退化；有 baseline 差异和性能回退机制。
6. Astra/Opus 按风险矩阵完成独立审核；所有 C0/C1 关闭后方可合并。
7. 写 `TASKS.md`、`STATUS.md`、`DECISIONS.md` 与 `verification/reviews/<ID>/`，记录 Merge SHA 与遗留问题。

## G. 统一评测协议

- 区分 **冷启动**（导入/编译/Plan/初始化）与 **稳态**（1000+ 或根据模型成本足够的时间步）和 **含结果 I/O 的整体作业**。10 步短测试不能替代长时显式模型端到端性能。
- CPU 比较：Serial、1/2/4/8/16/32（实际支持才测）；P-core only、E-core only、mixed；运行至少 5 次，报告中位数、P95、抖动及操作系统调度状态。
- 报告包含原始数组对照、View、SIMD 批处理、Gather/Scatter、Plan 构建、material、contact、producer/consumer 队列、读写/压缩、RSS/peak。
- 参考求解器比较：相同几何/网格、材料、本构、荷载、约束、接触、计算时长、dt 控制、精度、输出频率、CPU 资源、磁盘和误差阈值。记录许可证可用性。引用数据来源，无法公平匹配则不下定论。
- 能量与数值：验证外力功 W、动能 K、内能 U、沙漏能 H、接触耗散 D 等的适用平衡关系；与算法理论一致地允许有限数值误差，明确符号、离散时间层和归一化。
- 确定性采用分级契约：同 build/同拓扑/同并行调度尽力逐位；跨 SIMD 宽度、不同 GPU/MPI 规模采取有解释的容差等级。不得要求无法实现的所有硬件逐位一致而不计代价。
- **性能 KPI 为待校准值**：`View/raw ≤ 1.05`、`8T效率 ≥ 70%`、`GPU vs32核心 ≥5×`、`16节点强扩展 ≥60%` 目前是方向性研究门槛，不是当前结果或默认合并的刚性标准。

## H. 本轮推荐的前 3 次 GLM 提交

1. **提交一：R0 基线与证据**，绝不先做大规模重构。固定 SHA/版本/测试/Bench/CPU topology/GPU readiness，并把此路线整理进入仓库。
2. **提交二：R1.01 + R1.02 的最小正确性补丁**，修复无输出时间步的无谓物理重算/快照构造；新增分段重启、载荷/材料状态时刻验证；此提交必须 Astra + Opus 双审。
3. **提交三：R2.01 + R2.04 的诊断与性能对照**，只在测试有证据后改调度、View/SIMD，避免以抽象设计取代基准。

以上三批通过验收后，才允许 GLM 自主根据依赖图继续滚动选择任务。遇到架构重决策（修改公共 ABI、ModelIR Schema、文件格式、材料历史定义）必须先生成 ADR 并发 Astra/Opus 设计审核。
