---
name: petal-run-experiment
description: 在 Petal 上运行实验并查看结果。当需要运行单次或批量实验、把结果入库、通过 petal-lab 的 Web 前端或 JSON/数据库查看指标与图表、或排查实验失败原因时，应使用本 skill。它面向二次开发者的核心闭环：改完算法（petal-add-learner）→ 编译通过（petal-build）→ 跑实验 → 看效果。
---

# 运行 Petal 实验并查看结果

## 用途

把 `petal-lab` 串进"二次开发"的闭环：写完学习器（见 `petal-add-learner`）、编译通过（见
`petal-build`）之后，怎么跑一次真实实验、把结果存起来、并通过 Web 前端解读指标与可视化图表。

## 何时使用

- 改完代码、构建通过后，想跑一组实验验证
- 需要批量比较多个数据集 × 多个算法
- 想在 Web 界面查看学习曲线 / ROC / 混淆矩阵
- 某次运行没有产生结果，需要排查

## 前置

- 已 `make -j8` 生成顶层 `petal` 可执行文件。`petal-lab` 会自动按 `../../petal` 定位它；
  找不到时用 `--petal /绝对路径/petal` 指定。
- 已构建前端：`cd lab/web && npm install && npm run build`。`serve` 读的是 `lab/web/dist`，
  否则网页只有 API、看不到任何图表。

> ⚠ **若改过 `src/` 下的 C++ 引擎代码，跑实验前必须先 `make -j8`。** 否则 `petal-lab`
> 调用的仍是旧的 `./petal` 二进制，实验结果与你的修改对不上，排查时极易误判为
> “算法不对 / 数据不对”，其实是没重编。改 Python（`lab/petal_lab/`）则无需重编，
> 但需重启 `serve`。

## 三类入口

### 1. 单次实验

```bash
./lab/petal-lab run -- <数据集.meta> <数据集.data> -x5 -l<learner> [-d]
```

- 例：`./lab/petal-lab run -- data/weather.pmeta data/weather.pdata -x5 -lnb`
- `--` 之后是**原样传给 petal** 的参数；`--json` 由 `petal-lab` 自动插入，无需手写。
- `--batch <名称>`：把这次运行归入某批次（不存在则自动创建），之后在 Web 的"批实验"里聚到一起对比。
- `--dump-predictions`：同时导出逐样本预测，前端才能画 ROC / PR 曲线与混淆矩阵。

### 2. 批量实验（数据集 × 算法网格）

```bash
./lab/petal-lab batch run --name <批次名> \
    --datasets <数据目录或 .pm 文件> \
    --learners nb,aode,tan \
    -- -x5 -d
```

- `--datasets`：一个目录（自动扫描其中的 `.pmeta`/`.pdata` 对）或多个 `.pmeta` 文件。
- `--learners`：逗号分隔；`--` 之后是传给 petal 的公共参数（如 `-x5 -d`）。
- 全部结束后，打开 Web → "批实验" → 批次名，可逐次查看，也可横向对比。

### 3. 启动可视化

```bash
./lab/petal-lab serve [--port 8899] [--open]
```

- 同时提供 Web 前端与 `/api/*`（结果数据库查询）。
- 浏览器打开 `http://127.0.0.1:8899`。
- 页面入口：**总览 / 运行实验 / 单次运行 / 批实验**。
  - **运行实验**：选数据集、学习器、模式（交叉验证 `-xN` / 学习曲线 `-c`），提交后流式显示 petal 输出。
  - **单次运行**：点列表中任意一次运行查看详情——指标 chips、学习曲线、折间分布、ROC/PR、混淆矩阵，均支持 SVG/PNG 导出。
  - **批实验**：列出所有批次，点进去看批次内每一次运行（同样可展开详情），并为整批生成横向对比图。

## 结果落盘位置

所有运行写入 `lab/petal.db`（SQLite，已被 `.gitignore` 忽略）：
`runs` / `metrics` / `curves` / `confusion` / `predictions` / `batches`。

无需懂数据库即可用——Web 前端就是它的可视化外壳。若想直接查：

```bash
./lab/petal-lab list --limit 20      # 最近运行一览
./lab/petal-lab reset --yes          # 清空全部记录（保留表结构）
```

petal 本身还会把一份机器可读的 `schema:1` JSON 写入临时文件，由 `petal-lab` 摄入数据库；
那是前端图表真正的单一数据源。

## 如何解读看到的图表

- **指标 chips**（如 `nb · 0-1_loss`）：汇总折（fold = -1）的均值。
- **学习曲线**：0-1 损失随训练样本量下降；阴影是多次试验的极差。
- **折间分布**：每折 0-1 损失的箱线图，比单均值更能反映稳定性。
- **ROC / PR**：需要 `--dump-predictions` 才有；AUC chip 一并显示。
- **混淆矩阵**：行真实、列预测；类别不平衡时看 PR 比 ROC 更真实。

## 常见故障

**`找不到 petal 可执行文件`**
先 `make -j8`；或 `petal-lab ... --petal /绝对路径/petal`。

**运行结束但"结果未入库"**
petal 退出码非 0，或没产出 `--json` 文件。先用 `./petal <meta> <data> -x5 -lnb`
单独跑一遍，看 petal 自己的报错（能力声明不符、参数错误等）。

**Web 打开后图表空白 / 整页白屏**
确认 `cd lab/web && npm run build` 已执行（`serve` 读的是 `lab/web/dist`）。
若仍异常，通常是前端构建版本过旧——重新 build 并硬刷新浏览器（Cmd/Ctrl+Shift+R）。

**批量里个别 ✗**
该 (数据集, 算法) 组合 petal 跑失败（如算法不支持该数据类型）。
其余成功的仍会入库，不会中断整批。
