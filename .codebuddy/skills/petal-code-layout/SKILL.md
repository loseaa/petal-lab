---
name: petal-code-layout
description: Petal 项目的目录结构与代码组织约定。当需要定位某个功能的实现位置、判断新代码应放在哪里，或理解核心抽象（InstanceStream / Learner / 分布计数结构）时，应使用本 skill。
---

# Petal 代码布局

## 用途

说明 Petal 的目录划分、各模块职责，以及在何处查找特定功能。

## 何时使用

- 需要找到某个算法或机制的实现位置
- 不确定新代码应该放在哪个目录
- 需要理解核心抽象之间的关系

## 目录结构

```
petalAI/
├── Makefile / CMakeLists.txt / README.md
├── src/
│   ├── petal.cpp       入口：命令行解析与实验分派
│   ├── core/           核心抽象
│   ├── dist/           联合分布计数与条件分布树
│   ├── learner/
│   │   ├── bayes/      朴素贝叶斯、TAN、kDB 系、CBN、AODE / AnDE 系
│   │   ├── tree/       决策树与随机森林
│   │   ├── linear/     逻辑回归（L-BFGS / SGD / 子空间）
│   │   ├── ensemble/   Bagging、AdaBoost、Stacking、Feating
│   │   └── meta/       过滤器包装、外部学习器
│   ├── eval/           评估流程
│   ├── filter/         实例流过滤器与离散化
│   ├── io/             数据文件读写
│   └── utils/          工具、随机数、相关性度量
└── thirdparty/
    ├── alglib/         ALGLIB 数值库
    ├── lbfgs/          L-BFGS 优化器（C 代码）
    └── OPUSMinerCR/    关联规则挖掘
```

## 模块职责

### `src/core` —— 核心抽象

| 文件 | 职责 |
|---|---|
| `instance.*` | 单条样本：标称值数组 + 数值数组 + 类别 |
| `instanceStream.*` | 样本流游标抽象，五方法契约（rewind / advance / advance(instance) / isAtEnd / size） |
| `learner.*` | 学习器基类 |
| `incrementalLearner.*` | 增量学习器基类，实现多趟训练驱动循环 |
| `learnerRegistry.*` | 注册工厂（`std::map` + 静态注册器） |
| `capabilities.*` | 数据类型能力声明 |
| `*InstanceStream.*` | 各种流实现：间接流、存储流、打乱流、交叉验证子流等 |

### `src/dist` —— 分布计数（性能核心）

`yDist` → `xyDist` → `xxyDist` → `xxxyDist` → `xxxxyDist` 的递增维度家族，
以及 `distributionTree.*`（条件分布树，供 kDB / CBN 使用）、`xyGaussDist`（高斯）。

关键设计：**扁平化 + 上三角对称压缩**。只存储下标满足
`x1 > x2`（三维）、`x1 > x2 > x3`（四维）等组合，相比朴素多维数组可节省
1/2 至 23/24 的内存。

注意 `xxyDist.cpp` 与 `xxyDistEager.h` 中仍保留作者未完成的单块内存版本
（`#if 0` 死代码），如需进一步优化可从此处入手。

### `src/learner` —— 学习器

按算法族分五个子目录，详见 `petal-add-learner` skill。

### `src/eval` —— 评估

`xVal`（交叉验证）、`trainTest`、`streamTest`（prequential）、
`learningCurves`、`biasvariance`、`dataStatistics`。

### `src/filter` —— 过滤器与离散化

各类 `instanceStream*` 装饰器（离散化、归一化、类别过滤、特征构造等），
以及 `MDLDiscretiser`（MDL 准则）与 `eqDepthDiscretiser`（等深）。

### `src/io` —— 数据读写

`instanceFile.*` 实现 `.pmeta` / `.pdata` 格式解析（也支持 LIBSVM 格式）。

### `src/utils` —— 工具

`utils.*`、`random.*`、`mtrand.*`、`correlationMeasures.*`（互信息等度量）。

## 核心抽象关系

```
InstanceStream  ──提供──>  instance  ──喂给──>  Learner
                                                  │
                                          IncrementalLearner
                                          （多趟：reset → train×N → finalise）
                                                  │
                                          内部维护 *Dist 计数结构
```

- 学习器不持有数据，`InstanceStream` 是有状态游标。
- **训练前必须 `rewind()`**——流不会自动回到开头，复用同一流训练两次
  若不 rewind 会读到空数据。
- 遍历的统一写法是 `while (is.advance(inst)) { ... }`；
  遍历结束后流停在末尾，下次使用前需 rewind。

## 放置新代码的判断依据

| 新代码性质 | 位置 |
|---|---|
| 新的分类算法 | `src/learner/` 下对应族 |
| 新的概率分布计数结构 | `src/dist/` |
| 新的评估方式 | `src/eval/` |
| 新的数据预处理/变换 | `src/filter/` |
| 新的数据格式支持 | `src/io/` |
| 通用工具函数 | `src/utils/` |
| 第三方库 | `thirdparty/`（记得登记到 `SRC_DIRS`） |

## 定位技巧

项目 `#include` 是扁平写法，因此按文件名搜索即可定位：

- 找算法实现：在 `src/learner/` 下搜类名
- 找某个 `-l` 名称对应的实现：搜 `LearnerRegistrar` 的注册名
- 找某个命令行选项的处理：在 `src/petal.cpp` 中搜选项字符
