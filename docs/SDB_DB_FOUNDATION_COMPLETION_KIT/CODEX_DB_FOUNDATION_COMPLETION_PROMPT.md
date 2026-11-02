# 持续实施任务：Solver Database 基础数据类型与存储契约补齐

仓库：Nidofy/fem-solver-database
指定起点：codex/astra-lifecycle-result-hardening 的实际最新工作状态。
建议主线程：GPT-6 Astra / High。
建议子代理：GPT-6.1 Sol；盘点 Medium，实现/反例测试 High。最多同时 3 个。

## 一、目标与证据规则

请先评审当前分支已完成的改动，再立即持续补齐 DB 的基础数据类型、语义、内存布局、持久化和查询闭环，不要只交付审查报告或新计划。

重点从上一轮“结果/重启动 hardening + 新数值单元扩展”收敛为：

**把已有 DB 补齐成可以表达、校验、保存、查询和恢复结构力学模型与状态的数据基础设施，而不是继续扩张求解器算法。**

远端分析未能读取该分支真实 HEAD，因此下面是待核验/补齐契约，不是对当前源码的缺陷判定。当前分支可能已经实现其中一部分；必须用实际代码和测试判定，复用已完成能力，禁止重复重构。

原有 Database、ModelIR、ResultDB、文件格式、Memory/Field、CUDA 通路都优先复用。不要新建平行基础设施，不强制迁移到 HDF5/SQL，不为了扩展字段把所有热数据改为字符串字典或逐值 variant。

本轮不要求新开发 C3D8R/S4R/B31 物理 Kernel、塑性、接触算法、MPI、多 GPU 或完整隐式求解器；已有物理能力保持并回归。新增 shell/beam/material 等数据模式可以先做到 storage-only，但必须有真实构建、校验、序列化、查询和测试；执行能力不足时明确拒绝，不能伪装为支持求解。

## 二、工作区与连续执行

1. 读取实际 AGENTS.md、README、verification、既有任务台账与交接记录。检查 Git status、branch、HEAD、worktree、近期提交与对应 Diff。
2. 固定 REVIEW_HEAD 与可证明的 BASE/merge-base；不要假定 main 是基线。实际改动评审必须包括目标分支继承的相关变动，不只看最后一条 commit。
3. 保留用户未提交修改和其他 Agent 工作。禁止 reset --hard、clean、擅自 stash、覆盖、force push、自动发布或合并稳定分支。
4. 当前工作树有人写入时，使用从明确提交建立的隔离 worktree；其他工作树未提交内容不是已接收基线。若创建本轮分支，可用 codex/astra-db-foundation-completion，但不要覆盖已有同名分支。
5. 不修改生产求解器或无关构建，不擅自引入新外部依赖。无法联网/设备缺失时完成可执行的本地任务，不编造验证。
6. 当前任务采用持续开发：代码/测试/提交后自动推进下一依赖满足项，不要每完成一个小任务就停止。发现 P0/P1 数据安全与正确性缺陷先修复再扩大依赖。
7. 不把自审当独立审核；自测后标记 READY_FOR_REVIEW。
8. 接近上下文限制时保存已验证提交、未提交修改、测试、阻断与下一条命令，不承诺会话结束后继续执行。

## 三、D0：审查和真实能力矩阵

可并行派发两个只读任务：
- Agent A：基础类型/Field/模型实体/State/材料历史，建立源码地图。
- Agent B：ModelStore/ResultDB/Restart/Schema/输入能力，检查持久化闭环与兼容性。
主线程读取分支 Diff、共享契约与最近测试，合并两份证据，不等候他们替你做全部设计。

为每项记录：
`capability | symbol/file:line | public API | runtime storage | serialization | reader/query | tests | status | evidence`。
状态区分 PRESENT_VERIFIED、PRESENT_UNVERIFIED、PARTIAL、MISSING、BLOCKED_ENV、DEFERRED。Parsed/Validated/Stored/Queryable/Reloadable/Executable/Verified 分开记录，不用一个总“已支持”替代。

优先检查的风险：
- 新增字段是否存在于 State，却遗漏于 Result/Restart；
- 材料历史 schema 仅比较数量，而忽略名称、顺序和版本；
- 截面、方向、材料参数变动是否遗漏在相关身份/缓存失效逻辑；
- ragged offsets、32/64 位转换、磁盘 count/length 是否可溢出；
- View、异步快照和 CUDA 源/目标 buffer 的生命周期；
- 未知物理输入是否被静默忽略；
- 不同 IP/section point/方向/时间层是否被展平到无法解释；
- Reader/sidecar 是否绑定错误模型、run 或提交范围。

每个 finding 给触发条件、后果、源码位置、复现/回归方案和严重级别。源码推断与已运行复现分开。不机械复述旧任务中的 forces 重算、空帧或全扫描问题；先确认是否已经修复。

完成必要的 D0 后直接编写第一项缺口的回归测试和实现，不能把整轮都用于写文档。

## 四、D1：统一身份、索引和字段语义

主线程负责共享契约，先复用现有类型，再逐步补齐：

1. 区分 ExternalLabel 与内部索引，节点、单元、实例、材料、截面、表面、字段、Step/Frame 使用不易误传的强类型 ID。验证包装尺寸和热路径开销。
2. 保留实例/来源命名空间和外部标签；建立并验证 SourceMap 双向映射。内部重排不改变外部身份。
3. 固定宽度 wire 类型。长度/偏移采用受检的 64 位编码；内部索引按实际容量选用，不盲目全改 64 位。若只支持 Index32，端到端明确上限并拒绝溢出；不能只加别名声称 Index64 已支持。
4. 复用 FieldRegistry，补 FieldDescriptor：scalar type、shape、component labels/order、剪切约定、entity association、位置、单位/量纲、坐标基、构形、物理时间层、持久性、缺测策略、schema/version。
5. 计数、ID、flag 不存为浮点；缺测与零值区分。bool wire 编码明确。
6. 单位未知可明确保存 UNSPECIFIED，INP 输入不擅自推断单位制。坐标、材料方向、载荷方向和结果坐标基分别建模。
7. 保留未来复数/频率/模态样本轴的版本化扩展点，但不实现本轮不使用的完整频域系统。

出口：类型、描述符、单位/方向引用可构建、校验、往返、查询；有空值、非法引用、索引溢出和布局重排测试。

## 五、D2：通用内存存储与访问

在现有 Field/Memory/CSR 上扩展，不重做 allocator：

- Dense：固定 shape、连续 SoA/AoSoA，预解析 typed views；
- Ragged：offsets+values，用于混合 IP、材料历史、铺层和变长连接；
- Sparse：实体 ID/index + values + validity，用于区域结果、稀疏初值等；
- 沿用 CSR/BSR，补维数、索引位宽、block size、排序/重复项、对称标记的契约。

确保 offsets 单调、边界和总长一致，检查所有 count×components×sizeof 等算术。空段、零实体、超限、非法 offset 有测试。

冷名称/描述使用现有元数据表或字符串池，不进入逐实体热点。host 端允许类型分派，冻结布局后热点不逐值查表、反射或虚调用。

给出 borrow/read/write、重分配、generation、move/destruction 和 CPU/CUDA 同步规则。失效检查本身不得访问已销毁的 owner。按需分配，不给纯实体模型无条件分配壳梁/温度/隐式额外状态。

## 六、D3：模型、材料、截面、属性和历史 Schema

1. 区分 Topology、Formulation、Interpolation、IntegrationRule；补 edge/face/local point 的一致映射与版本。不要将 C3D8R 当 C3D8 或把存储能力当数值执行能力。
2. Part/Instance/集合/表面/参考点/刚体等复用现有定义，完善引用、作用域、面侧和来源映射。
3. 材料使用 schema ID/version、命名参数、范围/单位、表格自变量与插值/越界策略。不把所有扩展都塞到无类型 double[]/string map 中。
4. HistorySchema 描述变量名、shape、类型、初始化、持久/试算语义，按材料与积分点布局分组打包。相同变量数量不同语义不得认为兼容。
5. 补 solid、shell thickness/offset/layer stack、beam section/orientation 的可校验存储模式；不在本轮实现对应物理公式。
6. 属性常数、表格、区域分布与方向绑定在模型编译后仍保持可追溯。
7. 对未知材料算法可允许已知 schema 的 storage-only 保存，执行时拒绝；未知必需状态 schema 不得盲目重启。

每种新增类型必须有 builder、validator、查询、序列化与负例，不能只有 enum 或 placeholder。

## 七、D4：DOF、分析、约束、荷载与输入中间表示

1. 自由度语义与编号分离；只分配所需 DOF。
2. 表达 SPC、线性方程/MPC、参考点/刚体/耦合/tie 数据及作用区域。无算法的约束可保存不可执行，不擅自替换为罚约束或 SPC。
3. 节点/面/体载荷，方向、坐标、幅值、随动标志、Step 范围明确。
4. 幅值时间域、插值/越界、初始条件及多步延续/替换/激活语义可保存和查询。
5. 复用 ModelIR 和 Compiler，不创建 Abaqus 专用计算 DB。
6. 扩展输入能力仅针对已完成的数据契约：优先 Abaqus 的材料/截面/方向、集合/表面、幅值/载荷、方程约束等受控子集；逐项核实格式规范并建立 fixture。
7. 明确 parse / store-only / execute 三种行为；未知物理语义执行 fail-closed，允许忽略的纯元数据列白名单并保留诊断。

## 八、D5：State、Contact、Result、Restart 的同一语义链

统一可定位的数据地址，使其至少能区分：
模型/网格版本、实体身份、位置/局部点号、层/截面点、字段/分量、Step/采样轴、物理时间层。

- 支持混合积分点数和不同材料历史长度；层与截面点不能被不可逆展平。
- State 中注册的持久变量有明确 Restart policy；Derived/Transient 不盲目持久化。
- 联系现有 ContactDB：持久摩擦/接触历史和可重建候选缓存分开，动态对象有 generation/稳定来源。
- 字段描述随结果文件保存；scalar/vector/tensor/integer flag、局部坐标、缺测、时间层可从独立 Reader 正确理解。
- 初始帧/末帧/多步/分段恢复保留输出调度和结果提交水位。
- 结果采集不得推进材料/接触历史；异步 Writer 拿到拥有生命周期的快照。
- 重启先加载/校验临时对象，再发布。失败不能留下半恢复状态。
- 重新建立 Host/Device residency，不序列化设备指针或信任旧运行时驻留标记。

## 九、D6：持久化版本、迁移和资源安全

沿用当前真实版本的 .sdbmodel/.sdbres/.sdbrst；不按历史计划机械改成“v3”。

要求：
- logical schema/wire version/material state version/layout epoch 各自定义；
- scalar width、字节序、shape、组件顺序、feature flags、必需/可选块自描述；
- 不直接 dump C++ struct/STL/size_t/padding；
- 旧合法文件有 reader 或显式迁移，保留跨版本 golden fixture；
- 未知必需块拒绝；未知可选块的跳过/保留策略有记录，不能宣称已理解；
- 语义模型身份、分析身份、布局代次区别处理，不让重排改变外部身份，也不让材料/截面变动绕过匹配；
- CRC 仅用于损坏检测，非密码学 fingerprint 不作为防篡改证明；
- 解码前校验长度、偏移、资源限额；压缩有解压预算；
- 正常查询与恢复扫描分开；索引绑定 run、模型、文件代次及提交范围；
- 原子可见、进程崩溃恢复、断电耐久分别说明，不凭 rename/单一测试全称保证。

Schema 迁移必须随新增类型逐步接入并测试，不能最后统一补。

## 十、D7：综合验收与持续交付

建立明确标记为 synthetic-storage-fixture 的测试模型：
两个实例复用标签、实例变换、混合 topology/IP 数、不同 HistorySchema、壳层/截面点、梁截面方向、节点/单元/IP/面/接触/global 结果、张量顺序/局部基、整数与缺测、线性约束/多种载荷/曲线、稀疏区域、激活状态。

该模型只证明存储，人工填写的样本不冒充真实物理解。
另用现有真实求解子集跑 INP→Model→Compute→Result→独立查询→Restart 兼容回归。

测试至少覆盖：
- 新类型 API、查询和独立进程 round-trip；
- 同长度但不同语义的 schema 拒绝/显式迁移；
- 旧文件、未知块、缺失引用、截断、错误偏移、溢出、资源预算；
- 恢复失败原子性、异步所有权、结果不影响机械状态；
- float/double，CPU serial/OpenMP；有环境再验证 CUDA 编译和真机；
- 实体/字段/帧增长的内存与 IO 数据，不承诺未经测量的固定倍数；
- 独立小样本 oracle，避免 writer/reader 共享同一错误而自洽通过。

复用既有台账，在 docs/development/db_foundation/ 或仓库已有对应位置交付：
- 实际分支评审报告与已修复项；
- CAPABILITY_MATRIX；
- 类型和存储契约、格式兼容表；
- commit→任务→测试映射；
- 命令/退出码/日志/性能数据；
- 已实现、未验证、环境阻塞、明确延期项；
- 固定 SHA 的独立审核包和 RESUME 指令。

## 十一、子代理使用方式

共享 ID/FieldDescriptor/Schema registry/文件格式注册表仅主线程或明确唯一 owner 可写。先冻结小型契约再并行，不允许不同代理各自建立 incompatible schema。

建议：
- explorer：Sol Medium，只读盘点与证据；
- storage_worker：Sol High，独占指定 IO/迁移文件；
- model_worker：Sol High，独占指定实体/IR/适配文件；
- test_reviewer：Sol High，测试与反例或只读审核；
最多同时 3 个，不必一次启动全部角色。主线程整合、处理共享契约和冲突。

每次委派给出任务、实际 BASE_SHA、冻结接口、可写路径、禁止路径、完成标准、返回证据。代理不能自行改期望值掩盖 Bug，不能提交其他代理修改，不能主动合并稳定分支。

困难竞态或格式迁移的定向复审才考虑 Astra XHigh；不默认所有线程最高强度。实际宿主不支持指定模型/强度时记录实际使用配置，不伪称切换成功。不得为了使用外部模型擅自更换 provider、凭据或权限。

现在执行 D0，并在本次会话条件允许范围内持续推进第一批基础类型与存储闭环，不以文档或接口声明代替完成。
