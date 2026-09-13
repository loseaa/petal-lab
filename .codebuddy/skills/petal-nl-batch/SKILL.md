---
name: petal-nl-batch
description: 用自然语言描述一批实验，由 agent 调用 petal-lab 的 batch from-text 子命令解析并执行，结果自动入库。当用户用口语/中文描述“用哪些数据集、比较哪些算法、看什么指标”的实验需求，并希望直接跑起来时使用。
---

# 自然语言批量实验

## 用途

让用户用一句大白话表达实验意图，agent 把它转成 `petal-lab` 的批处理命令并执行。
底层与「运行实验」Web 表单、以及 `petal-lab batch run` 走**完全相同**的入库通路：
解析出的都是「数据集 × 算法」的网格，每个组合经由 `builders.build_command` 校验后调用
`petal`，结果写入 `lab/petal.db`（SQLite），Web 前端「批实验」页直接可见。

## 何时使用

- 用户在对话框里说：「用所有 UCI 数据集比较 nb、aode、tan 的准确率」
- 「跑一下 weather 和 iris，10 折，算法用 j48 和 knn」
- 任何想把想法直接变成一批实验、而不想手动拼命令的场景

## 前置

- 已 `make -j8` 构建顶层 `petal` 二进制（`petal-lab` 自动定位 `../../petal`）。
- 已 `cd lab/web && npm run build`。
- 数据放在 `data/` 或 `examples/`，使用 `.pmeta`/`.pdata` 后缀（一对同名文件）。

## 执行步骤

1. 让用户把需求说清楚（数据集范围、算法、指标/折数）。
2. 在 PetalAI 项目根目录执行：

   ```bash
   ./lab/petal-lab batch from-text "<用户原话>" -y
   ```

   - `-y` 跳过二次确认直接执行；想先预览计划可去掉 `-y`，脚本会打印解析出的
     批次名、数据集、算法、指标/模式/折数及等效命令，再手动确认。
3. 解析走两条路径（自动选择）：
   - **未配大模型**：`PETAL_LLM_PROVIDER` 为空或未设置 API key 时，走本地规则
     匹配数据集名 / 算法名 / 指标关键词（零配置可用）。
   - **已配大模型**：设置 `PETAL_LLM_PROVIDER=openai|anthropic` 与对应 key 后，
     由模型做结构化解析，输出仍经同一套参数校验。
4. 命令逐对打印 (数据集, 算法) 的执行结果（✓/✗），并提示在 Web「批实验」查看。

## 说明

- 解析只产出**参数**，绝不拼 shell 命令；最终仍由 `builders.build_command` 的
  allow-list（算法名白名单、路径限制在 `data/`/`examples/` 内）把关，
  安全边界与手动敲命令完全一致。
- 想看横向对比：启动 `./lab/petal-lab serve --open`，打开「批实验 → 批次名」。
- Web 前端不内置大模型生成；自然语言这一层只在对话框里通过本 skill 完成，
  页面本身以「勾选参数」为主（运行实验 / 批实验表单）。
