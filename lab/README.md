# petal-lab

petal 实验结果管理：执行、入库、可视化。

设计目标是**尽量轻**：后端只用 Python 标准库（`sqlite3` + `http.server`），
没有任何第三方依赖，也不需要编译。前端仅在构建期使用 npm，产物是静态文件。

## 安装

无需安装。要求 Python 3.9+。

```bash
cd lab/web && npm install && npm run build   # 只需一次，产出静态前端
```

## 日常使用

## 在页面上跑实验

侧边栏「运行实验」页可以直接启动 petal，不必回到终端：

1. 选择数据集、模式、算法、离散化等参数
2. 点「运行」
3. 输出实时显示在右侧终端，进度条显示当前折数
4. 完成后自动入库，并可一键跳到结果图表

实现要点：

- **实时输出用伪终端（pty）**。petal 的 stdout 若连到管道，C stdio 会切成全缓冲，
  输出只能成块（4KB）吐出甚至最后一次性出现；pty 让 petal 以为连着终端，
  从而恢复行缓冲。实测 3 万条数据、103 行输出：管道与 pty 都能流式，
  但只有 pty 的行为是确定的（输出少时也逐行到达）。
- **进度来自 petal 的 `-v2` 输出**，形如 `0-1 loss (fold 5)`，解析出折号即真实进度。
- **推送用 SSE**（`text/event-stream`），不需要 WebSocket，标准库即可。
- **安全**：只绑定 127.0.0.1；参数按列表传递（绝不经 shell）；
  算法名与选项走白名单正则；数据集路径必须位于允许的目录内
  （默认 `data/` 与 `examples/`）。

任务在后台线程执行，页面关掉也会继续跑；重连时能看到完整日志（服务端保留最近 3000 行）。

## 两种实验

这两类实验回答不同的问题，因此在界面上分开管理：

| | 单次运行 (Runs) | 批实验 (Batches) |
|---|---|---|
| 规模 | 1 个数据集 × 1 个算法 | N 个数据集 × M 个算法 |
| 回答 | 这一次表现如何 | **整体上哪个算法更好** |
| 图表 | 学习曲线、折间分布、ROC/PR、混淆矩阵 | 平均排名、临界差异图、胜负平、数据集×算法热力图 |

### 单次运行

```bash
./lab/petal-lab run -- \
    examples/weather.pm examples/weather.pd -dmdl -x10 -lnb
```

`run` 是 petal 的包装器：照常写 petal 的参数，它会自动加上 `--json`、
执行、把结果写入 SQLite。不需要手工导出任何文件。

需要 ROC / PR 曲线时加 `--dump-predictions`。

### 批实验（多数据集 × 多算法）

```bash
./lab/petal-lab batch run --name grid1 \
    --datasets /path/to/datasets \
    --learners nb,aode,tan \
    -- -dmdl -x10
```

一条命令自动展开成 N×M 次 petal 执行，全部归入同一批次：

```
批实验「grid1」
  4 个数据集 × 3 个算法 = 12 次运行
  [1/12] syn_easy.pd · nb  ✓
  ...
完成 12 次，失败 0 次。
```

`--datasets` 接受目录（自动查找 `.pm` 及同名 `.pd`）或具体 `.pm` 文件。
`--` 之后的参数会原样传给每一次 petal 调用。

### 查看结果

```bash
./lab/petal-lab serve --open      # 默认 http://127.0.0.1:8899
```

页面分两块：

- **实验历史** — 按数据集/学习器/模式筛选，点选一次运行查看细节图表
- **批处理对比** — 跨数据集的统一统计（见下）

### 其他命令

```bash
./lab/petal-lab list                 # 列出历史
./lab/petal-lab import a.json b.json # 导入已有的 JSON 结果
```

## 跨数据集比较怎么做

直接平均误差率是误导的：难数据集上的 0.30 可能优于简单数据集上的 0.05。
因此这里采用 Demšar (JMLR 2006) 的标准做法：

1. **平均排名** — 在每个数据集内部对算法排名，再跨数据集取平均，
   从而消除数据集本身的难度差异
2. **Friedman 检验** — 判断这些算法整体上是否有显著差异
3. **Nemenyi 后验 + 临界差异 (CD)** — 排名差小于 CD 的算法用粗横线连接，
   表示"差异不显著"
4. **Win / Draw / Loss** — 两两胜负计数
5. **数据集 × 算法热力图** — 暴露"某算法在特定数据集上失效"这类问题

页面会显示 N、k、χ²、临界值和 CD，便于直接写进论文。

> 注意：Friedman 检验需要足够多的数据集才有统计效力（通常 ≥ 10 个）。
> 数据集很少时"无显著差异"是样本量不足的结果，不等于算法等价。

## 图表样式与导出

**「论文模式」是干什么的？**

它把图表切换成适合放进论文的样子，解决的是"截图贴进论文会糊、被嫌丑"的问题。
开启后整个界面的图表立即变为：

- 衬线字体（Times 系），与论文正文一致
- 黑色坐标轴线、去掉网格线——期刊通常要求简洁的坐标轴
- 字号放大到 12–13 pt，缩小到版面宽度后仍可读
- 页面配色转为黑白，便于判断最终排版效果

关闭时是彩色屏幕样式，适合探索数据。开关在左侧栏下方，切换即可看到差异。

**导出**：每张图右上角有 `SVG` 和 `PNG` 两个按钮。

- **SVG** — 矢量图，可无损缩放，也能用其他工具（如 Inkscape、`rsvg-convert`）转 PDF，
  这是投稿最稳妥的格式
- **PNG** — 4 倍缩放渲染，约合 300 dpi，适合直接插入 Word/PPT

建议流程：探索时用屏幕模式 → 定稿前开论文模式 → 导出 SVG 转 PDF 放进论文。

## 结构

```
lab/
├── petal-lab              命令行入口
├── petal.db               SQLite 数据库（首次运行自动创建）
└── petal_lab/
    ├── db.py              schema 与连接
    ├── ingest.py          JSON → 数据库
    ├── stats.py           平均排名 / Friedman / Nemenyi / W-D-L
    ├── server.py          HTTP API + 静态托管
    └── cli.py             run / import / list / serve
└── web/
    ├── src/               React + Vega-Lite 源码
    └── dist/              构建产物（由 serve 托管）
```

## 数据存放

`runs.raw_json` 保存 petal 输出的原始 JSON，即使结构化表的定义日后改变，
也能重新解析；同时拆出 `metrics` / `curves` / `confusion` / `predictions`
等表，使跨数据集聚合可以用 SQL 表达。

每次运行的完整命令行也存在库里（`runs.command`），便于复现。

## API

| 端点 | 说明 |
|---|---|
| `GET /api/runs?dataset=&learner=&mode=&batch_id=` | 运行列表 |
| `GET /api/runs/<id>` | 单次运行详情 |
| `GET /api/runs/<id>/predictions` | 逐样本预测（自动降采样至 2 万条） |
| `GET /api/batches` | 批次列表 |
| `GET /api/compare?batch_id=&metric=` | 跨数据集统计 |
| `GET /api/datasets` | 数据集列表 |

服务默认只绑定 `127.0.0.1`，不对外暴露。
