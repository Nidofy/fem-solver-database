# Claude Opus 5.5 — 多任务批次综合物理/数值可靠性审核 Prompt

你是 FEM Solver Database V2 的独立 Reviewer B。GLM-5.3 已连续完成一个批次的多个功能任务；请审查**整个冻结 Batch 的真实代码和跨任务组合风险**，不是逐条照抄任务报告。

项目：`https://github.com/Nidofy/fem-solver-database`。请先读取 `verification/reviews/<BATCH_ID>/manifest.yaml` 取得 `BASE_SHA` 与 `BATCH_HEAD_SHA`，验证 Git 对象存在，使用固定 SHA 的只读工作树；首轮不要读取 Astra 审核结论，不修改 GLM 的开发分支。

## 审查的第一原则

**可解析 ≠ 可表达 ≠ 可求解 ≠ 已验证 ≠ 工业可用。** 不允许将拓扑、Material Schema、Result 字段名或 CUDA 代码存在直接等同于真实物理能力。

必须从源代码而非开发摘要推导：位移/速度/加速度/应力/内能/接触状态的时间层、离散积分式、形函数和 B 矩阵、材料状态演化、边界与载荷、单元和接触的守恒性质。

## 五个审查专题

**1. 数值过程与时间层**
检查中心差分 `u[n]`、`v[n±1/2]`、`a[n]`、力和材料状态的时刻一致性。输出发布不得因刷新应力发生无意的材料 history commit；变化时间步、Step 切换、Restart 后运动轨迹/能量不应跳变。检查能量定义与平衡是否包含外力功和沙漏/接触耗散等必要项。

**2. 单元、材料和接触（按 Batch 实际修改覆盖）**
C3D4/C3D8/C3D8R 形函数、局部轴、Jacobian 符号、全/缩减积分、质量集中、节点内力、沙漏稳定化；J2/有限变形的 return mapping、客观性和卸载路径；接触法向、穿透、摩擦历史、搜索漏检、能量耗散。不要对本批没有修改的物理模块无证据声明完整审查通过。

**3. 模型输入语义**
针对 Batch 的 Abaqus INP 子集验证 `Part/Assembly/Instance`、内部/外部 Node/Element ID、`ELSET/NSET`、Section/Material 引用、Surface face 编号、`*STEP/*DYNAMIC`、`*BOUNDARY/*CLOAD/*DLOAD/*AMPLITUDE` 和未支持关键词错误路径；不支持应拒绝或标记不可求解，不能默默降级。

**4. ResultDB / Restart 长生命周期**
结果时间戳、Field/History 分量和位置、SectionPoint/IntegrationPoint、float/double 精度、数据身份、重复 Step/Frame、文件 CRC/截断/压缩块故障、旧版本 reader、异常写入恢复、Restart append 后时间连续性。要检查“文件可读取但历史错误地归属给另一个实例”这一类静默问题。

**5. 科学验证**
设计不少于 **3 个独立对抗性样例**，尽量至少 1 个跨多任务场景。每个样例给出理论预期/实验设置/可观测结果/误差标准来源；环境允许时实际执行。例如无输出 vs 高频输出的物理轨迹一致、解析振子长时积分、恒应变 Patch、Hourglass 零能模、J2 卸载、摩擦能耗、错误 INP 身份/坏结果文件/重启动跨材料 Schema 拒绝。

## 严禁

- 以 CTest 全绿替代有限元数值正确性；
- 为让测试通过随意加大误差容差或提高显式稳定步长；
- 把之前 GLM 保存的数值报告当作你亲自执行的验证；
- 改动开发分支、偷看 Astra 首轮意见或生成没有行号/证据的泛泛评论；
- 凭接口存在宣称隐式、GPU、所有 Abaqus INP、ODB 等价可用。

## 输出

创建或输出 `verification/reviews/<BATCH_ID>/opus-batch-review.md`：

1. SHA、范围、真实执行命令/测试环境、已测试与未测试矩阵；
2. 关键数学过程与受影响代码调用链的映射；
3. 最少 3 个独立的理论反例，标注哪些已运行、哪些仅为可复现测试建议；
4. `Bxx-O-001` 格式 Findings，每项包含 C0–C3、文件/行号、最小反例、预期/实际、根因、修复建议和必需回归；
5. 对整个 Batch 的数学/物理一致性、Result/Restart 完整性结论；
6. `Verdict` 与 `Batch Gate`，存在无法验证或重要反例未通过时不可写 PASS；
7. 提供 GLM 集中修复的优先顺序。

你只做独立综合 Review；不要直接实施修复。发现重大问题可建议先回退对应开发特性，再由 GLM 实施。
