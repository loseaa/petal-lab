---
name: petal-build
description: 构建、调试与验证 Petal 分类学习系统。当需要编译项目、排查构建错误、清理产物，或理解本项目的 include 约定与构建清单时，应使用本 skill。
---

# 构建与验证 Petal

> ⚠ **关键：改了 C++ 必须重新编译，否则改动根本不生效。**
> `petal` 引擎是 C++ 编译出的二进制；`petal-lab`（网页 / server）每次运行调用的都是
> 这个 `./petal` 文件。凡是改动了 `src/`（或 `thirdparty/`）下的任何 `.cpp` / `.h`，
> 都必须重新 `make`（见下文）生成新的 `./petal`，改动才会被真正运行到。
> 只改了 `lab/petal_lab/` 下的 Python 不需要重编 C++，但需**重启 server** 才能生效。

## 用途

提供 Petal 的构建、调试、清理与常见故障排查流程。

## 何时使用

- 修改了源码后需要编译验证
- 构建报错需要定位原因
- 需要理解为什么某些文件没有参与编译

## 构建命令

```bash
make            # Release 构建，产物 ./petal
make -j8        # 并行构建（推荐，全量约需数分钟）
make debug      # 调试版本（-g -O0），会先 clean
make clean      # 清理 .o / .d / 可执行文件
make run ARGS="-x10 -lnb"   # 构建后带参数运行
```

也提供 `CMakeLists.txt`，但当前环境未安装 cmake，因此 **Makefile 是实际构建路径**。

## 目录与 include 约定

源码位于 `src/`（按功能分模块）与 `thirdparty/`（alglib / lbfgs / OPUSMinerCR）。

**所有 `#include` 一律写成扁平形式**，例如 `#include "xyDist.h"`、
`#include "instanceStream.h"`，不写相对路径。构建时通过 `-I` 把各模块目录
加入搜索路径，因此移动文件不需要修改任何源码。

新增模块目录时，把目录加进 `Makefile` 的 `SRC_DIRS` 即可自动纳入编译与头文件搜索。

## 不参与构建的文件

以下两个文件被显式排除，与原始 `petal.cbp` 的收录范围保持一致：

| 文件 | 原因 |
|---|---|
| `src/learner/bayes/kdbCDRAM.cpp` | 依赖 `dtCatNode` 中受 `_kdbCDRAM` 宏保护的条件成员；未定义该宏时无法编译。原工程只登记了 `kdbCDRAM.h` |
| `src/learner/bayes/kdbGaussian.cpp` | 未接入的实验性学习器。原工程只登记了 `kdbGaussian.h` |

两者也未在 `learnerRegistry` 注册，因此排除它们不影响任何可用功能。

若确需启用 `kdbCDRAM`，须同时定义 `_kdbCDRAM` 宏，并确认
`distributionTree.h` 中对应的条件成员已完整实现。

## 验证流程

```bash
make -j8                       # 1. 构建
./petal                        # 2. 无参数应输出用法
./petal data.pm data.pd -x5 -lnb   # 3. 用真实数据跑一个学习器
```

第 3 步需要自备数据集（.pmeta 元数据 + .pdata 数据）。格式见 README 的"数据格式"一节。

## 常见故障

**`fatal error: xxx.h: No such file`**
新增了目录但未登记。把目录加进 `SRC_DIRS`。

**`undefined reference to ...`**
源文件未被 wildcard 收集到。确认文件在已登记目录下且扩展名正确。

**`no member named 'valuesCount_' in 'dtCatNode'` 一类错误**
误编译了 `kdbCDRAM.cpp`。检查 `EXCLUDED_SRCS` 是否仍包含它。

**链接期 `multiple definition of main`**
仓库中只有 `src/petal.cpp` 含 `main`。若新增文件引入了 `main`，需移除或排除。

**macOS 上 `<malloc.h>` 找不到**
非标准头文件。项目已在 `thirdparty/OPUSMinerCR/find_rules.cpp` 中改为按平台选择
`<stdlib.h>`；新增代码请使用标准头文件。

## 清理

`make clean` 会移除各模块目录下的 `.o` 与 `.d` 以及可执行文件。
它们已被 `.gitignore` 排除，不会进入版本库。
