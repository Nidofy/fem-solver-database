# Solver Database：基础数据类型与存储补齐计划

日期：2026-10-29
目标仓库：Nidofy/fem-solver-database
本地路径：D:\Codes\Solver\ExplicitSolver_track_l_c0a_l0\solver_database_v2
用户指定分支：codex/astra-lifecycle-result-hardening

## 0. 证据边界：这不是当前 HEAD 的通过性代码审查

本次未成功取得目标分支的树、README、提交历史、原始源码或压缩包。容器中 Git 访问也未取得远端代码。能够读取的是默认分支 `codex/standalone-database` 的公开页面快照；该快照不能替代指定分支的实际状态。没有取得目标 HEAD，没有完成 Diff 审查，没有编译目标分支，也没有执行该分支测试。

因此本文不提供“该分支已经完成/尚未实现某函数”的确定结论，也不虚构当前缺陷的行号。本文提供的是：
1. 可确认的旧公开基础；
2. 需要在目标分支逐项验证的基础契约和审查风险；
3. 发现缺口后直接实施的有界开发计划。

当前缺口状态统一为 `UNVERIFIED_AT_TARGET_HEAD`，由本地 Codex 在 D0 对照真实源码改成 `PRESENT_VERIFIED / PRESENT_UNVERIFIED / PARTIAL / MISSING / BLOCKED_ENV / DEFERRED`。既有实现通过验证后复用，不能因本文列出了某项就重复开发。

## 1. 已有基础与本轮范围

公开默认分支 README 列出：Field<T,C,Layout>、SoA/AoSoA、MeshDB/StateDB、积分点历史打包、旋转字段、事务、ContactDB、CSR/BSR、Checkpoint、CUDA 镜像，以及 INP 子集到 ModelIR、原生结果文件和重启动的生命周期接口。[S1]

这些说明项目不是“从零做 DB”。但有容器或声明，不等于具有完整语义与持久化闭环；默认分支说明也不证明用户指定分支的最新状态。

### 本轮目标

补齐结构力学显式优先、后续隐式可用的基础数据模式和存储契约。每个新增能力必须覆盖：

`构建/导入 → 校验 → 冻结/布局 → 访问 → 持久化 → 重载/查询 → 兼容性测试`。

### 本轮不扩大

不新增第二套 Database / ModelIR / ResultDB，不强制改用 HDF5、SQL 或对象图；不为证明存储而实现新的 C3D8R、S4R、B31、塑性、接触、MPI、多 GPU 或完整隐式算法。已有数值能力保持并做回归。无法求解但可合法保存的模型，使用明确的 storage-only 路径，并在执行时拒绝。

## 2. 目标分支的第一轮评审清单

下列是需核验的风险，不是已证实的目标分支 Bug。

| 风险级别 | 核验问题 | 最小证明 |
|---|---|---|
| P0 | 新旧 Schema 不一致时是否把数据读错但仍继续运行 | 独立进程加载旧合法文件、新文件、未知必需块、不同材料状态版本 |
| P0 | 材料/截面/方向等影响物理意义的变动是否被身份检查遗漏 | 单独修改材料、截面、局部方向、分析条件，验证相应缓存/恢复失配 |
| P0 | 动态字段、材料历史、接触历史是否全部进入恢复事务 | 枚举持久字段，注入恢复失败，确认发布前完整、失败不改变原对象 |
| P0 | typed view、只读快照、CUDA 拷贝是否可能在源重分配/销毁后仍被访问 | 借用期重分配、销毁、异步完成顺序及取消测试 |
| P0 | 变长数据 offset/count、索引位宽和磁盘长度是否可能溢出或错位 | 接近位宽上限的算术测试、伪造长度/偏移、截断、资源预算 |
| P1 | 保存的结果是否带足够位置与物理语义 | 同一单元多个 IP/层/截面点、局部坐标、时间层、缺测值往返 |
| P1 | Reader 取得身份与索引却查询到另一个模型/run 的数据 | 错配索引、过期 sidecar、不同 mesh revision、重复外部编号 |
| P1 | 解析成功是否被混同为可以求解 | 保存合法未支持 formulation；启动求解明确失败且诊断定位到输入 |
| P1 | 性能优化是否以丢弃字段、重复 full copy 或隐藏同步为代价 | 相同工作量基准；内存、复制字节、读写字节与传输计数 |

审查意见格式：`ID / severity / BASE..HEAD / file:line / symbol / trigger / impact / reproduction / fix / verification`。源码推断与实际复现分开。已关闭的历史问题不再重复登记。

## 3. 三层类型，而不是一个大结构体

### 3.1 语义层

表达节点/单元/实例/材料/截面/坐标系/约束/载荷/分析步/结果字段等工程概念，包含来源和维数。可以使用易维护的 host 端对象、注册表和变体。

### 3.2 运行存储层

模型编译为分组、连续、类型化、按需分配的 SoA/AoSoA 和扁平变长池。热点只访问预解析 View，不在每个实体上查字符串、不使用逐值 variant、不通过逐对象虚调用读数据。CPU/GPU 可选不同布局，但逻辑身份和变量含义不变。

### 3.3 持久化层

明确固定宽度、字节序、Schema、块类型、长度、校验与索引。磁盘格式不能依赖 C++ 对象地址、padding、size_t、编译器 enum 布局、STL 容器对象体或具体 SIMD 宽度。

统一语义不等于把三层强行规定成相同的二进制布局。

## 4. 基础数据契约的补齐目录

### F1. 身份、标量和索引

- 区分 ExternalLabel、NodeId、ElementId、MaterialId、SectionId、InstanceId、FieldId、SurfaceId、StepId、FrameId，避免整数误传。可采用无额外存储成本的强类型包装；不要为每个小 ID 分配对象。
- 外部身份包含来源命名空间、实例和外部标签；内部索引允许重排。SourceMap 必须有前后映射并经过模型、结果和恢复文件保留。
- 磁盘长度/偏移使用明确的 64 位编码。内部 32 位索引可以保留；所有导入与分配边界做 checked conversion/加乘检查。
- 64 位内部索引若尚无全链支持，记录上限并拒绝超限；禁止只改 typedef 宣称大规模支持。
- 明确非法 ID、generation 与失效规则。激活/删除状态不要悄悄重编号，旧结果要仍可定位原实体。
- 计数、ID、flag 使用整数，不借用 float/double。布尔的 wire 编码、validity bitmap 与 GPU 表达要明确。

Abaqus 节点标签允许在不同 part/instance 中重复，因此仅使用“节点号”不足以作为跨模型层身份。[S2]

### F2. FieldDescriptor：字段值的解释契约

复用已有 Registry，补齐统一 FieldDescriptor，而不是建立平行注册系统。建议的字段如下（实现可合并拆分）：

- 标量种类：FP32/FP64 和有明确需要的固定宽度整数；
- shape/component labels：标量、向量、对称张量、一般张量、矩阵；
- component order、工程剪应变/张量剪应变约定；
- entity association 与输出位置：node、element、element-node、IP、layer/section point、face、contact sample、global；
- 单位/量纲或明确 UNSPECIFIED；
- 坐标基和方向引用；
- 参考/当前构形、物理时间层、状态阶段；
- dense/ragged/sparse 存储描述、缺测/无效策略；
- persistent/transient/derived、checkpoint policy、schema/version。

位置、时间和缺测不能靠数组长度猜测。缺少结果与物理值为零必须可区别。复数/频率/模态轴可先保留明确扩展点，不为本轮虚构复杂算术和求解支持。

Abaqus 的结果位置包括积分点、截面点、表面、节点和整体模型，方向又能影响张量分量的解释；这仅作为外部语义参照，不是对本仓库当前状态的判断。[S3][S4]

### F3. 单位、坐标与方向

- 区分节点坐标变换、材料方向、梁截面方向、荷载方向、结果坐标基。
- 支持局部坐标引用的校验、父子关系和循环检测；定义旋转/变换顺序及浮点容差。
- 张量基变换必须尊重分量顺序及剪切约定。不得在只有数组的情况下推断“这是工程应变”。
- 单位信息缺失时保留未知，不擅自从数值大小推断 SI 或 mm-N-s。
- 进入核函数前显式完成必要转换，热点不依赖动态单位系统。

Abaqus 输入要求自洽单位但没有内置统一单位制，因此 INP 接口应记录用户声明或未知，而不能自动认定单位。[S5]

### F4. 通用存储原语

在现有 Field/CSR 基础上补齐三类复用机制，不重做内存库：

1. Dense：固定 shape、连续数组、SoA/AoSoA View。
2. Ragged：offsets + values，适用于混合 IP 数、不同历史长度、铺层和变长连接。校验首尾 offset、单调性、count 和长度；支持边界安全的分段 View。
3. Sparse：实体 ID/局部索引 + values + validity，可用于部分区域输出、稀疏初始条件和稀疏状态。定义顺序与重复项规则。

数值矩阵沿用已有 CSR/BSR，补足 dimension、index width、block size、对称性、重复项及排序契约；不要在 DB 任务中自研新线性求解器。

字符串、名称、描述等冷数据使用紧凑字符串池或现有 metadata 表。不能放进逐节点热结构。压缩块不强行 mmap 零拷贝；先保证有界解码与选择性访问。

### F5. 元素描述与模型实体

- Topology 与 Formulation、Interpolation、IntegrationRule 分开；存储某种 topology 不代表可执行它的某个 formulation。
- 固定/变长连接、边/面表、面朝向、法向侧、IP 布局有明确版本与引用。
- 材料/截面/方向绑定不能在布局重排后失配。
- Part/Instance/Assembly、集合、Surface、参考点、刚体的语义身份保留。
- 增加实体描述时要实现查询、校验和往返，不以一个 enum + opaque blob 当作有语义支持。
- 未支持算法的实体仍可用于 storage-only 测试，但禁止执行时悄悄替换。

### F6. Material / Section / Property / History Schema

- 材料有 schema ID/version、命名参数、单位/范围、可选温度/应变率等表格、自变量及插值/越界策略。
- 绑定 per-schema 历史变量描述：名称、shape、scalar type、初值、持久性、trial/commit 策略。
- 历史布局按材料/公式/积分点组编译；不使用能容纳所有材料最大状态量的统一大结构体。
- 支持 solid、shell thickness/offset、layer stack、beam section/orientation 的数据模式和校验；不要求相应数值 Kernel。
- 属性可绑定常数、表格、区域分布或方向。未知必需材料 Schema 不能进入求解或盲目恢复。
- 同 schema ID 但变量含义或顺序变化也属于兼容性问题，不能只比较历史变量数量。

### F7. 分析、自由度、约束和荷载

- DOF 语义与编号分开；只给实体分配需要的平移/转动/其他受支持自由度。
- 表达 SPC、线性方程约束/MPC、参考点、耦合/刚体/tie 的数据，不把它们均降格为一个 constrained 位。
- 荷载区分节点、面、体，记录方向、坐标、幅值、随动标志、适用 Step/区域。
- 时间曲线的时间域、插值、越界和更新规则有定义。
- 初始条件、多步激活/延续/替换的语义可保存和查询；实际可执行组合单独校验。
- 隐式只补齐已有矩阵与试算状态的数据契约，不扩张完整算法。

### F8. StateDB / ContactDB / ResultDB 的通用地址与生命周期

建议在语义层能唯一确定：

`模型/网格版本 + 实体身份 + 位置/局部点号 + 层/截面点 + 字段/分量 + Step/样本轴 + 物理时间层`。

- 不同元素、材料、层、截面点的状态数可变，必须可独立定位。
- 持久 contact history 与可重建的候选/BVH cache 分开；动态采样实体要有身份和有效期。
- Runtime 的材料/积分点 Schema 必须贯穿结果与 Restart；禁止求解器有字段、结果/重启遗漏。
- 只读结果导出不得推进机械状态；异步 writer 的数据须有受控所有权。
- 分别记录 committed 状态和 trial 状态的发布边界；不得错误地把 speculative 状态当重启动状态。
- Device residency 在重载时重新建立，不持久化 CUDA 指针或把旧设备有效标记当成恢复依据。

### F9. 文件 Schema 和兼容层

复用原 `.sdbmodel/.sdbres/.sdbrst` 生态：

- 明确 logical schema version、wire version、material schema version 和运行布局代次，各自职责不同。
- 必需/可选块、特性标志、未知字段、旧缺省值、迁移路径明确。
- 未知可选数据只有在承诺支持的模式下保留/跳过；跳过不是已解释。缺少未知必需语义时拒绝执行。
- 语义模型身份、分析身份、布局代次分离：重排不改变来源标签；影响物理的材料/截面变化会失配；输出配置修改是否可续写由显式策略判断。
- 校验和不是认证，非密码学指纹不是碰撞免疫证明。
- 新 schema 流式写入，旧文件逐版本读回；恢复先验证后发布，错误不留下半修改状态。
- 不假定一次 rename 等于所有平台上断电耐久；分别声明原子可见性、进程故障恢复和持久化边界。

## 5. 建议阶段和验收

| 阶段 | 主要任务 | 出口 |
|---|---|---|
| D0 | 固定真实 HEAD/BASE；评审本分支改动；建立逐能力证据矩阵 | 每条结论绑定实际源码/测试，不重复旧任务 |
| D1 | IDs/index/offset、FieldDescriptor、坐标/单位、Schema 身份 | 编译期/运行期类型与越界测试；字段描述可持久化 |
| D2 | Dense/Ragged/Sparse、变量池、View/所有权契约 | 空段/混合长度/重排/异常/CPU-GPU边界测试 |
| D3 | Element/Material/Section/Property/History 数据模式 | 混合模型可构建、保存、查询，未知算法执行被门禁拦截 |
| D4 | Analysis/DOF/约束/荷载/曲线与来源映射 | typed IR 到冻结 DB 无损，storage-only 与 executable 分开 |
| D5 | 通用状态地址贯穿 State/Contact/Result/Restart | 混合 IP/层/方向/时间层查询与恢复一致 |
| D6 | Schema 版本与原格式升级、未知块策略、恢复原子性 | 旧合法文件可读；错配、损坏、超限明确失败且不污染原对象 |
| D7 | Abaqus 定向接入、CLI/Reader 集成、完整回归与性能证据 | 输入→存储→查询→重启闭环，真实求解仅验证已支持子集 |

D1 是共享契约冻结点。其后 D3/D4 可在隔离文件中并行，D6 的测试和读写实现可先围绕冻结 Schema 开展，但具体迁移必须随新增字段完成，不拖到最后才接。每个里程碑都应是可编译的小型纵向闭环。

## 6. 一套检验基础是否足够通用的测试模型

创建明确标记 `synthetic-storage-fixture` 的数据集：

- 两个实例复用局部标签，其中一个有平移旋转；
- 多个 topology/formulation 的存储描述，混合积分点数；
- 线弹性材料与测试材料 Schema，历史变量长度不同；
- shell layer/section-point 数据及 beam section 方向数据；
- 节点、单元、IP、截面点、面、接触采样、global 结果；
- 张量分量顺序、局部方向、整数状态、缺测值；
- SPC 与线性约束方程、节点/面/体载荷及曲线的存储；
- 激活状态变化和稳定身份，稀疏区域输出。

该模型用于证明存储能力，不得将人工填入的数据标记为物理计算结果或商业软件对标。另用仓库已有真实 Kernel 运行完整求解回归。

必须测试：独立进程 round-trip、可观测 API 语义等价、多个 IP/section-point 随机查询、旧格式 golden fixtures、未知必需 schema、错误 offset、资源预算、异步失败、恢复失败原子性、CPU serial/OpenMP float/double 及环境具备时 CUDA。

“写后能被同一 writer/reader 读回”不足以证明正确：还要独立 oracle、人工可检查小样本和跨版本样本，防止写读共享相同错误。

## 7. 完成定义

每一项能力分别记：`Parsed / Validated / Stored / Queryable / Reloadable / Executable / Verified`，不是一个简单的线性等级。例如 ModelBuilder 创建的数据不必经 Parser；可存不可解是合法完成状态。

一个基础数据类型完成的最低要求：

1. 有明确身份、shape、单位/方向/时间/位置语义（不适用项明确）；
2. 有可执行的构建与校验；
3. 有实际存储与类型化访问；
4. 有序列化、重载、查询；
5. 有旧版/异常/容量边界处理；
6. 有测试和资源开销证据；
7. 不支持计算时明确禁止执行。

没有代码、只有设计文档或 enum，不算完成。执行测试缺环境标记 NOT_RUN，而不是 PASS。自测完成状态为 READY_FOR_REVIEW，不冒充独立审核通过。

## 8. 主线程与子代理

本轮建议主线程用 GPT-6 Astra / High，负责共享契约、生命周期、版本迁移决策与整合。这是针对跨模块基础契约任务的工程分工建议，不是声称在本仓库完成了模型能力对比测试。

子代理建议：

| 角色 | 模型/强度 | 权限边界 |
|---|---|---|
| 源码盘点、已完成项映射 | GPT-6.1 Sol / Medium | 初期只读；返回 file:line 和测试依据 |
| 持久化/迁移实现 | GPT-6.1 Sol / High | 只改分配的文件；遵守冻结 Schema |
| 数据实体/输入适配实现 | GPT-6.1 Sol / High | 不改共享类型文件，必要改动交主线程 |
| 反例、旧格式、故障注入测试 | GPT-6.1 Sol / High | 独立 oracle，不迁就实现修改期望 |
| 共享契约高风险复审 | Astra / High；难点局部 XHigh | 冻结 SHA 只读检查，不再次接管实现 |

最多同时 3 个子代理，通常 2 个写代码 + 1 个只读审查/测试；共享类型与格式注册表只设一个写入者。XHigh 不常驻所有线程，只用于复现困难的竞态、迁移不兼容等明确问题。

已有 Opus 5.5 可继续在支持它的宿主里做冻结版本独立 review；不假定它能直接作为官方 Codex 内置子代理，也不假定不同宿主的同名 thinking 档位数值等价。

官方 Codex 文档允许指定子代理模型与 reasoning effort；未覆盖时可能继承父配置。角色分工及明确模型配置比在 Prompt 中含糊地写“多开几个代理”更可控。[S6] 官方推理指南把 High 用于复杂推理和调试，XHigh/更高强度需用实际效果证明额外成本合理。[S7]

## 9. 来源

[S1] 本次可读的默认分支页面快照（不是目标分支）：
https://github.com/Nidofy/fem-solver-database

[S2] Abaqus Node Definition：
https://docs.software.vt.edu/abaqusv2025/English/SIMACAEMODRefMap/simamod-c-node.htm

[S3] Abaqus Output to the Output Database：
https://docs.software.vt.edu/abaqusv2025/English/SIMACAEOUTRefMap/simaout-m-dboutput-sb.htm

[S4] Abaqus Orientations：
https://docs.software.vt.edu/abaqusv2025/English/SIMACAEMODRefMap/simamod-c-orientation.htm

[S5] Abaqus Units：
https://docs.software.vt.edu/abaqusv2025/English/SIMACAEGSARefMap/simagsa-c-absunits.htm

[S6] 官方 Codex 子代理文档（访问原地址后重定向至 Learn）：
https://developers.openai.com/codex/subagents

[S7] OpenAI reasoning guide：
https://developers.openai.com/api/docs/guides/reasoning

所有设计建议与目标分支事实严格分开；必须在 D0 依据真实 HEAD 更新证据矩阵。
