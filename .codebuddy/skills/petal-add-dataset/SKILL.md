---
name: petal-add-dataset
description: 在 Petal 中新增一个数据集供实验使用。当需要准备自己的数据、了解 .pmeta/.pdata 格式、把数据放到正确位置并让网页 / CLI 自动识别时，应使用本 skill。
---

# 新增 Petal 数据集

## 用途

把一份新数据接入 Petal，使其出现在网页「运行实验 / 批实验」的数据集列表里，并能被
`petal` 引擎读取、跑分类实验、结果自动入库。

## 何时使用

- 手头有自己的 CSV / 表格数据，想拿来跑分类实验
- 不清楚 `.pmeta` / `.pdata` 的格式与命名约定
- 放好文件后网页里看不到，或 petal 报「文件不存在 / 缺少类别列」

## 文件与命名（关键）

每个数据集由**两个**文件组成，必须成对出现：

| 文件 | 作用 | 后缀（务必用这个） |
|---|---|---|
| 元数据 | 定义每个属性名、类型、取值 | `.pmeta` |
| 数据 | 每行一个实例 | `.pdata` |

> ⚠ **后缀必须是 `.pmeta` / `.pdata`，不是 `.pm` / `.pd`。** 项目早期示例曾用 `.pm`/`.pd`，
> 现已统一为 `.pmeta`/`.pdata`；用错后缀系统扫描不到，petal 也会找不到文件。

## 格式

`.pmeta`（元数据）——每行一个属性：

```
属性名 : 取值1, 取值2        # 标称属性，列出全部可能的取值，逗号分隔
属性名 : numeric             # 数值属性
:class: 类别属性名 : 取值1, 取值2   # 类别（目标）列，必须声明
```

例（与 `examples/weather.pmeta` 完全一致）：

```
:class: play : no, yes
outlook : sunny, overcast, rainy
temperature : numeric
humidity : numeric
windy : false, true
```

`.pdata`（数据）——每行一个实例，属性值按 `.pmeta` 中的顺序用**空格或逗号**分隔，
类别值放在 `:class:` 声明所对应的位置：

```
sunny 85 85 false no
overcast 83 86 false yes
rainy 70 96 false yes
```

## 放置位置（对接到系统）

把这对文件放到 **`data/`**（或 `data/uci/` 等子目录）下即可。后端 `server.py` 的
`DEFAULT_DATASET_ROOTS = ["../data", "../examples"]` 会递归扫描这些目录，自动发现所有
`.pmeta`/`.pdata` 对。

- 放好后**无需改任何代码**，也无需重启（`availableDatasets` 每次打开网页都会重新扫描磁盘）。
- 启动 `./lab/petal-lab serve` 后，在网页「运行实验 / 批实验」里即可直接看到新数据集。
- 终端快速验证：`petal <x>.pmeta <x>.pdata -x5 -lnb` 能跑通即格式正确。

## 常见陷阱

- **数值属性**：朴素贝叶斯等算法不支持，跑之前加 `-d`（如 `-dmdl`）离散化，
  否则报 `Naive Bayes does not support numeric attributes`。
- **类别列缺失 / 顺序错**：报 `missing class column`。检查 `.pmeta` 是否用 `:class:`
  声明了类别，且 `.pdata` 每行的类别值位置与之匹配。
- **只放了一个文件**：`.pmeta` 必须有同名的 `.pdata` 才被识别（按后缀互换）。
- **后缀写错**：用 `.pm`/`.pd` 会导致扫描不到，网页列表里不会出现。

## 对接流程（零代码改动）

1. 按上面格式写好 `你的数据.pmeta` / `你的数据.pdata`。
2. 复制到 `data/`（或任意子目录）。
3. 打开网页 → 「运行实验」或「批实验」→ 数据集列表里即出现「你的数据」。
4. 选算法、设折数、提交，结果自动入库并在「运行记录 / 批次」里可视化。
