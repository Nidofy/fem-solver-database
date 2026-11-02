# 使用说明

1. 先阅读 DB_FOUNDATION_REVIEW_AND_PLAN.md 第 0 节。本次没有获取指定分支真实 HEAD；文件不是该分支的代码审核通过报告。
2. 在本地仓库中使用 Astra / High，发送 CODEX_DB_FOUNDATION_COMPLETION_PROMPT.md 全文。主线程会先以真实代码核对能力，再持续实施缺口。
3. 子代理默认 Sol / High；只读盘点可用 Medium。最多同时 3 个，公共 Schema 仅一个 owner。
4. CODEX_CONFIG_MERGE_EXAMPLE.toml 只是可选合并示例，不直接覆盖已有配置，也不更改 provider/权限/认证。与本机 Codex 版本不兼容时使用界面或受支持角色配置。
5. 本轮主要补基础数据模式和存储；不是继续增加物理 Kernel。已有实现通过验证后直接复用。

交付内容：计划/评审清单、完整持续实施 Prompt、可选配置示例。
