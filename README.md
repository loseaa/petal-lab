# Petal

Petal —— 面向大数据的分类学习系统（Version 0.2）。

原始作者：Geoffrey I. Webb（Monash University）等。
本仓库是在原始代码基础上做的**工程整理**（构建系统、跨平台兼容、文档），算法实现未作改动。

---

## 构建

### 方式一：Makefile（推荐，无需额外依赖）

```bash
make            # 构建 Release 版本，产物为 ./petal
make -j8        # 并行构建
make debug      # 构建带调试符号的版本（-g -O0）
make clean      # 清理
```

### 方式二：CMake

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
```

### 说明

- 需要 C++11 编译器。在 macOS 上 `g++` 实际是 Apple clang，可直接使用。
- `lbfgs.c` 是 C 代码，由 `CC` 编译（对应原 Code::Blocks 工程中的 `compilerVar="CC"`）。
- 以下两个文件**不参与构建**，与原始 `petal.cbp` 保持一致：

  | 文件 | 原因 |
  |---|---|
  | `kdbCDRAM.cpp` | 依赖 `dtCatNode` 中受 `_kdbCDRAM` 宏保护的条件成员；未定义该宏时无法编译。`petal.cbp` 只登记了 `kdbCDRAM.h` |
  | `kdbGaussian.cpp` | 未接入的实验性学习器。`petal.cbp` 只登记了 `kdbGaussian.h` |

  两者也未在 `learnerRegistry` 中注册，因此不影响任何可用功能。

---

## 用法

```
petal <metafile> <datafile> [-p<posClassName>] [<test method args>] -l<learner> [<learner args>]
```

### 快速开始

仓库自带示例数据 `examples/weather.{pm,pd}`（200 条样本，含标称与数值属性）：

```bash
make -j8

# 决策树可直接处理数值属性
./petal examples/weather.pmeta examples/weather.pdata -x10 -ldtree

# 贝叶斯类只支持标称属性，需先用 -d 离散化
./petal examples/weather.pmeta examples/weather.pdata -dmdl -x10 -lnb
./petal examples/weather.pmeta examples/weather.pdata -dmdl -x10 -laode

# 训练/测试与流式评估
./petal examples/weather.pmeta examples/weather.pdata -dmdl -texamples/weather.pdata -laode
./petal examples/weather.pmeta examples/weather.pdata -dmdl -s -laode
```

| 目的 | 做法 |
|---|---|
| 减少输出噪音 | 加 `-v0`，只输出指标不打印模型 |
| 查看模型结构 | 默认 `verbosity=1`，决策树会打印树形 |
| 处理数值属性 | 贝叶斯类加 `-dmdl`；树与线性模型可直接用 |
| 确认学习器是否支持当前数据 | 直接运行，能力不符会明确报错（如 `Naive Bayes does not support numeric attributes`） |

评估方式（互斥）：

| 选项 | 含义 |
|---|---|
| `-x[<folds>[,<exps>]]` | 交叉验证，默认 10 折 1 次 |
| `-t<testfile>` | 在 `<datafile>` 上训练，在 `<testfile>` 上测试 |
| `-s` | 流式评估（prequential，先测试后训练） |
| `-b<...>` | bias/variance 实验 |
| `-c` | 学习曲线 |
| `-e<...>` | 外部交叉验证 |
| `-z` | 数据统计 |

其他常用选项：

| 选项 | 含义 |
|---|---|
| `-l<name>` | 指定学习器（见下表） |
| `-p<name>` | 二分类化，指定正类名 |
| `-d...` | 离散化过滤器（`dynamic` / `dynamicPID` / `mdl` / `mdl2` / 等深） |
| `-n` | 数值归一化 |
| `-f` | 基于 OPUS Miner 的特征构造 |
| `-v<n>` | 详细级别，默认 1 |
| `--json=<file>` | 把结构化结果（指标/学习曲线/混淆矩阵）写成 JSON，供可视化前端（如 lab/web）读取 |
| `--dump-predictions` | 在上述 JSON 里附带逐样本预测概率，用于画 ROC/PR；默认关闭，因为体积是 O(样本数×类别数) |

示例：

```bash
./petal data.pm data.pd -x10 -laode
./petal data.pm data.pd -ttest.pd -lkdb
./petal data.pm data.pd -s -lnb

# 产出结构化结果，再用 lab 的 Web UI（petal_lab serve）打开查看图表
./petal data.pm data.pd -x10 -lnb --json=result.json --dump-predictions
```

### 输出精度

全系统统一：**所有对外输出的小数一律保留 4 位小数**（固定小数点，非有效数字）。

- 唯一定义在 `src/core/globals.h` 的 `PETAL_FLOAT_FMT`（即 `"%.4f"`）。
- C++ 侧所有 `printf` / `fprintf` / `sprintf` 的浮点转换以及 `--json` 产出的
  JSON 数值都通过该宏写出，不在调用点手写精度。
- 前端 `web/app.js` 的 `fmt()` 与 `lab/web` 的 `toFixed(4)` / `format: '.4f'`
  与之对齐，保证页面读到的数与终端打印的数一致。

改动位数只需改 `PETAL_FLOAT_FMT` 一处。

> 例外：`src/learner/bayes/kdbCDdisc.cpp` 的 `toString(float)` 写的是供外部
> 离散化器读取的临时文件，属于数据序列化而非结果展示，因此保留原有精度。

---

## 学习器

`-l` 可使用的名称（来自各 `.cpp` 顶部的 `static LearnerRegistrar`，共 23 个）：

| 名称 | 类别 | 说明 |
|---|---|---|
| `nb` | 贝叶斯 | 朴素贝叶斯 |
| `tan` | 贝叶斯网络 | Tree Augmented Naive Bayes |
| `kdb` | 贝叶斯网络 | k 依赖贝叶斯，默认 k=1（`-l kdb+k<k>`） |
| `selective-kdb` | 贝叶斯网络 | 带属性筛选的 kDB（`-lselective-kdb+k2`） |
| `cbn` | 贝叶斯网络 | 受限贝叶斯网络，需指定 `+d<dof>` 或 `+k<k>` |
| `aode` | AnDE | 平均单依赖估计器 |
| `aodeExt` | AnDE | AODE 扩展 |
| `aode_pw` | AnDE | AODE 逐对加权 |
| `aodebse` | AnDE | AODE 带子sumption 消解 |
| `a2de` | AnDE | 平均二依赖估计器 |
| `a2de2` | AnDE | A2DE 变体 |
| `a2de_ms` | AnDE | A2DE 变体（模型选择） |
| `disa2de` | AnDE | 离散化 A2DE |
| `a3de` | AnDE | 平均三依赖估计器 |
| `a3de_ms` | AnDE | A3DE 变体（模型选择） |
| `dtree` | 树 | C4.5 风格决策树（增益率准则） |
| `rfdt` | 树 | 随机森林 |
| `lr` | 线性 | 批量多类逻辑回归（L-BFGS 优化） |
| `lrsgd` | 线性 | 随机梯度下降逻辑回归 |
| `subspace-lrsgd` | 线性 | 带子空间特征的逻辑回归 |
| `bagging` | 集成 | Bagging，默认 100 个基模型 |
| `filtered` | 元学习器 | 在过滤器链上训练指定学习器 |
| `dataStat` | 工具 | 输出数据统计，非分类器 |

### 未注册的学习器

以下 18 个类已被实现并编译进二进制，但**没有** `static LearnerRegistrar`，
因此无法通过 `-l` 调用（工厂查不到，会报 "Learner ... is not supported"）：

`aodeEager`、`aodeDist`、`a2je`、`kdbExt`、`kdbEager`、`kdbSelective`、
`kdbCDdisc`、`kdbCDRAM`、`kdbGaussian`、`AdaBoost`、`EnsembleLearner`、
`FeatingLearner`、`Feating2Learner`、`Feating3Learner`、`StackedLearner`、
`gnb`、`sampler`、`randomClassifier`

它们大多带实验性质的开关。若需启用，在对应 `.cpp` 顶部补一行注册即可，例如：

```cpp
static LearnerRegistrar registrar("kdbExt", constructor<kdbExt>);
```

---

## 数据格式

### 元数据文件（.pmeta）

文本格式，每行声明一个属性：

```
<line>      ::= [ : <qualifier> { , <qualifier> } : ] <name> : numeric
             | [ : <qualifier> { , <qualifier> } : ] <name> : <name> { , <name> }
             | :: <comment>
<qualifier> ::= width=<uint> | index=<uint>-<uint> | class
```

- `:class:` 限定符声明类别属性；属性名恰为 `class` 时也会被自动识别为类别
- 类型写 `numeric` 表示数值属性，否则需列出全部取值（标称属性）
- `numeric, ?` 声明该数值属性含缺失值
- `width=N` 表示定宽列，`index=A-B` 可展开为多个同名后缀属性（`name_A`…）
- `::` 开头为注释

示例：

```
:: 样本元数据
:class: class : a, b
x1 : v0, v1, v2
x2 : t, f
n1 : numeric, ?
```

### 数据文件（.pdata）

逗号分隔，列的顺序与 `.pmeta` 中的声明顺序一致；数值缺失写作 `?`：

```
a, v0, t, 1.5
b, v2, f, ?
```

也支持 LIBSVM 格式，此时元数据写作 `:libsvm: <属性数>`。

---

## 目录结构

```
petalAI/
├── Makefile / CMakeLists.txt
├── src/
│   ├── petal.cpp       入口：命令行解析与实验分派
│   ├── core/           核心抽象：instance / InstanceStream / Learner / 注册工厂
│   ├── dist/           联合分布计数（性能核心）与条件分布树
│   ├── learner/
│   │   ├── bayes/      朴素贝叶斯、TAN、kDB 系、CBN、AODE / AnDE 系
│   │   ├── tree/       决策树与随机森林
│   │   ├── linear/     逻辑回归（L-BFGS / SGD / 子空间）
│   │   ├── ensemble/   Bagging、AdaBoost、Stacking、Feating
│   │   └── meta/       过滤器包装、外部学习器（Weka / LibSVM / VW 等）
│   ├── eval/           评估：交叉验证、训练测试、流式、学习曲线、bias-variance
│   ├── filter/         实例流过滤器与离散化器（MDL / 等深）
│   ├── io/             数据文件读写
│   └── utils/          通用工具、随机数、相关性度量、JSON 输出与结果收集
├── tools/              构建辅助脚本（生成 clangd 用的 compile_commands.json）
├── web/                结果可视化页面（读取 --json 产出的文件，纯静态、可离线打开）
└── thirdparty/
    ├── alglib/         ALGLIB 数值库
    ├── lbfgs/          L-BFGS 优化器（C）
    └── OPUSMinerCR/    关联规则挖掘（供 -f 特征构造使用）
```

> **关于 `#include`**：代码中的头文件引用一律是扁平形式（如 `#include "xyDist.h"`）。
> 构建时通过 `-I` 把上述各模块目录加入搜索路径，因此**移动文件不需要修改任何源码**。
> 新增模块时，把目录加进 `Makefile` 的 `SRC_DIRS` 即可。

### 性能核心的一点说明

`xxyDist` / `xxxyDist` 等采用「扁平化 + 上三角对称压缩」存储：只保存
`x1 > x2`（或 `x1 > x2 > x3`）的组合，可节省 1/2 至 23/24 的内存。
`xxyDistEager.h` 中还留着作者未完成的单块内存版本（`#if 0` 死代码，`offset1/offset2/countSize`），
如需进一步优化可从这里入手；`xxyDist.cpp` 中同类代码已清理。

---

## 已知事项

- 原始 `petal.cbp`（Code::Blocks 工程）中的调试参数指向 Windows 路径 `d:\datasets\...`，
  在本机直接使用会失败，需自行修改。
- 学习器注册已从早期 `learnerRegistry.cpp` 中的 if-else 工厂链改为
  `std::map` + 静态注册器；那条 if-else 链现在仍以注释形式留在文件中，可安全忽略。

---

## 许可

原始代码为开源实现（GPL v3），详见各源文件头部声明。
`OPUSMinerCR/README.txt` 与原始论文引用参见该目录。
