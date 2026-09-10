---
name: petal-add-learner
description: 在 Petal 分类学习系统中新增一个学习器（learner）。当需要实现新的分类算法、把已有但未注册的学习器接入命令行、或理解 Petal 的学习器接口契约时，应使用本 skill。
---

# 新增 Petal 学习器

## 用途

指导在 Petal 中正确实现一个新学习器：选择目录、实现接口、注册到工厂、接入构建、验证。

## 何时使用

- 需要新增一个分类算法
- 需要让某个已实现但未注册的学习器能通过 `-l<name>` 调用
- 需要理解 `learner` / `IncrementalLearner` 的接口契约与训练趟次语义

## 关键背景

Petal 的学习器通过**静态注册器**接入，而不是集中式工厂。`learnerRegistry.cpp`
中早期那条 if-else 工厂链已被注释废弃，现在的机制是：

```cpp
static LearnerRegistrar registrar("名称", constructor<类名>);
```

工厂底层是 `std::map<std::string, PtrToLearnerConstructor>`，注册发生在静态初始化期。
因此**只写类不注册，学习器就完全不可见**——仓库里有 18 个这样的类（见 README 的
"未注册的学习器"一节）。

## 实施步骤

### 1. 选择目录

按算法族放入 `src/learner/` 下对应子目录：

| 目录 | 适用 |
|---|---|
| `bayes/` | 朴素贝叶斯、TAN、kDB 系、CBN、AODE / AnDE 系 |
| `tree/` | 决策树、随机森林 |
| `linear/` | 逻辑回归等线性模型 |
| `ensemble/` | Bagging、AdaBoost、Stacking、Feating |
| `meta/` | 过滤器包装、外部学习器（Weka / LibSVM / VW 等） |

不确定时参照最相近的现有实现。

### 2. 创建头文件与源文件

命名采用 `lowerCamelCase`（如 `kdbSelectiveClean.h/.cpp`），类名与文件名一致。

头文件需要继承 `learner` 或 `IncrementalLearner`：

```cpp
#pragma once
#include "incrementalLearner.h"

class myLearner : public IncrementalLearner {
public:
    myLearner(char* const*& argv, char* const* end);
    myLearner(const myLearner& l);
    learner* clone() const;
    ~myLearner(void);

    // IncrementalLearner 接口
    void reset(InstanceStream &is);
    void initialisePass();
    void train(const instance &inst);
    void finalisePass();
    bool trainingIsFinished();
    void getCapabilities(capabilities &c);

    virtual void classify(const instance &inst, std::vector<double> &classDist);
private:
    // ...
};
```

**构造函数签名必须是 `(char* const*& argv, char* const* end)`**——
`constructor<T>` 模板按此签名生成工厂函数。无参需求时留空实现即可。

### 3. 实现接口

两类接口二选一：

**批量学习器** —— 直接实现 `train(InstanceStream &is)`，自行遍历数据流。
适用于需要随机访问或物化数据的算法（决策树、逻辑回归、随机森林）。

**增量学习器** —— 实现 `IncrementalLearner` 的逐样本接口，基类会驱动多趟循环：

```
reset(is) → initialisePass() → [ train(instance) × N ] → finalisePass()
                                    ↑ 若返回 false 则 rewind 后重复
```

`finalisePass()` 返回 `trainingIsFinished()` 的结果；单趟算法直接 `return true`。

实现要点：

- `getCapabilities()` 声明能处理的数据类型；标称属性用 `c.setCatAtts(true)`，
  数值用 `c.setNumAtts(true)`。声明不符会在训练前被 `testCapabilities` 拦下。
- `classify()` 必须写出合法概率分布（各项非负且和为 1）；调用 `normalise()` 收尾。
- 数值属性通常需要先经离散化（`-d` 选项），不要假设能直接拿到数值。

### 4. 注册

在 `.cpp` 顶部（include 之后）加一行：

```cpp
static LearnerRegistrar registrar("myLearner", constructor<myLearner>);
```

注册名即命令行 `-l` 后的名字，需与其他学习器不重复；重复注册会触发断言。

### 5. 接入构建

源文件放在**已登记目录**下时无需改动构建配置——`Makefile` 与 `CMakeLists.txt`
按目录通配符收集源文件。

只有新增**目录**时才需要把它加进 `Makefile` 的 `SRC_DIRS`
（以及 `CMakeLists.txt` 的 `PETAL_SRC_DIRS`）。

### 6. 构建与验证

```bash
make -j8
```

确认无编译错误后做行为验证：

```bash
./petal <meta>.pm <data>.pd -x5 -lmyLearner
```

预期：输出各项指标，概率分布合法，准确率符合算法预期。

## 检查清单

- [ ] 放在 `src/learner/` 下正确的族目录
- [ ] 构造函数签名为 `(char* const*& argv, char* const* end)`
- [ ] 实现了 `clone()`（返回未训练副本，交叉验证多折依赖它）
- [ ] 实现了 `getCapabilities()` 且与实际处理能力一致
- [ ] `classify()` 输出已归一化
- [ ] 已加 `static LearnerRegistrar` 注册行
- [ ] `make -j8` 通过
- [ ] 用真实数据跑过一次并核对指标

## 常见陷阱

**忘记注册**——最常见。类编译通过但 `-l` 查不到，报
`Learner ... is not supported`。检查是否有 `LearnerRegistrar`。

**未加入构建**——若文件放在 `thirdparty/` 或某个未登记的新目录，
wildcard 不会收集它，表现为链接期符号缺失。

**训练趟数错误**——`finalisePass()` 返回值决定是否需要多趟。
单趟算法若误返回 `false` 会陷入死循环；多趟算法若过早返回 `true` 则模型未收敛。

**`clone()` 返回已训练对象**——交叉验证每折需要独立副本，
返回自身会导致折间状态污染，指标异常偏优。
