# skills/ —— 按身份组织的工作说明

**这份目录回答什么问题**：你以什么身份工作、能碰哪些文件、必须守哪些硬约束、交付成什么格式。

## 三种身份

| 身份 | skill | 权限（tag） | 写什么 |
|---|---|---|---|
| **AI 开发者** | [ai-app-developer/SKILL.md](ai-app-developer/SKILL.md) | 2 `aiapp` | 只写 `apps/demo/<app>/` + 注册表一行 |
| **项目开发者** | [project-developer/SKILL.md](project-developer/SKILL.md) | 3 `framework` + 1 `sysapp` | 框架机制、系统 App；契约与公共库属人工区 |
| **硬件平台开发者** | [platform-developer/SKILL.md](platform-developer/SKILL.md) | 4 / 5 / 6 | 只写 `platform/<自己那块板>/` |

为什么要按身份拆，而不是写一份"总说明"：

- **约束不同**。AI 开发者不能碰框架，平台开发者不能碰契约，项目开发者能碰机制但不能替所有人做决定。合成一份，每条都要加"除某某身份外"，读的人反而不知道该守哪几条。
- **上下文成本**。AI 开发者只需要知道自己那 6 个可用符号；把平台移植的验收顺序塞给它，是纯噪声。
- **触发不同**。`description` 是触发指纹——"写 App"和"移植新板"是两件永远不该互相命中对方的事。

## skill 是什么

一个目录 + 一个 `SKILL.md`，开头是 YAML frontmatter：

```markdown
---
name: ai-app-developer
description: 什么时候该读这一份（触发指纹：写清"何时"和"做什么"）
---
```

`description` 不是简介，是**检索条件**。写"关于 App 的一些说明"永远匹配不上。

复杂的 skill 可以带 `references/`、`scripts/`、`assets/`。本仓库暂时只有 `SKILL.md`。

## 谁能写

**只有人。** AI 能改 skill 就能删掉对自己不利的约束，然后依据改过的 skill 证明自己合规。`policy.txt` 里 `skills/` 对任何角色都没有 `w`。

## 身份 ≠ 权限

"我以项目开发者身份工作"不等于"拥有全部写权限"。`api/`、`common/`、`lib/`、`tests/`、构建文件在 `policy.txt` 里**没有交给任何角色**——它们是人写的。以 AI 身份做这类改动，产出是「提案 + diff」，落盘由人完成。

## 每份 skill 必须有的两节

1. **你能碰什么**——权限表 + 自检命令。没有这一节，AI 只能靠猜。
2. **触发边界：命中任一条，立刻停**——本身份的停止条件、停下时交什么、不要做什么。

第二节是硬要求，理由：skill 里全是"怎么做"，只有边界那一节说"什么时候不许做"。缺了它，AI 会把"没被禁止"当成"被允许"，越界之后还能给出看起来很完整的交付。

## 待补

- 三份 skill 的"先读哪份"由根目录 [AGENTS.md](../AGENTS.md) 的路由表决定；新增 skill 时**必须同时加一行路由**，否则进了仓库也没人读得到。
- `add-a-capability`：可以先并进 `project-developer`，等它长到读不完再拆。
