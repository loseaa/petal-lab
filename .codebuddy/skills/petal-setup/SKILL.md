---
name: petal-setup
description: 在任何操作系统（macOS / Linux / Windows-WSL）与任何智能体/编辑器下，一键初始化 Petal 实验环境——构建 C++ 执行内核、安装并构建 Python 服务端和前端，并跑通第一个实验。当用户说“初始化 petal 环境”“setup petal”“把环境搭起来”“build everything”或任何希望从零跑通本项目时使用本 skill。
---

# Petal 环境初始化

本 skill 让任何人在任何平台上、用任何工具（CodeBuddy、其他编辑器内置智能体、TRE、或任意能读 Markdown 的 Agent）把 Petal 实验环境初始化好并顺利跑起实验。

## 项目由三部分组成

| 部分 | 作用 | 构建 / 依赖 |
|---|---|---|
| **C++ 执行内核 `petal`** | 真正的分类学习引擎，跑交叉验证 / 训练测试，产出 `--json` 结果 | C++11 编译器 + `make`；`thirdparty/`（alglib / lbfgs / OPUSMinerCR）**已内置仓库，无需下载** |
| **Python 服务端 `lab/petal_lab`** | 接收批量实验请求、调度子进程、收集结果、提供 Web API 与统计（Friedman / Nemenyi 临界差异） | Python 3.8+，**仅用标准库，无第三方包** |
| **前端 `lab/web`** | 实验配置、实时进度、状态机、临界差异图等可视化 | Node.js 18+（`npm install` + `npm run build`） |

> 注：当前后端为纯标准库实现，没有 PyTorch 等额外依赖。若未来加入新的扩展（例如基于 PyTorch 的学习器），只需在本 skill 的“依赖”小节补充对应安装命令即可，引导脚本 `setup_env.py` 也相应扩展。

## 自然语言触发（用户这样对我说即可）

- 中文：“帮我初始化一下 petal 环境”“把环境从头搭起来，我要跑实验”“build everything / 初始化所有依赖”
- 英文：“setup the petal environment”“initialize petal”“build everything”

## 自动化初始化（推荐，任何 Agent 都能执行）

仓库根目录提供跨平台引导脚本 `setup_env.py`（纯标准库，无依赖）：

```bash
python3 setup_env.py
```

脚本会：检测 OS 与缺失的前置依赖 → 打印对应系统的安装命令（不擅自 sudo）→ 构建 `./petal` → `npm install && npm run build` 前端 → 校验产物。

## 手动初始化（按平台）

### 1) 前置依赖

| 依赖 | macOS | Linux (Debian/Ubuntu) | Windows |
|---|---|---|---|
| C++11 编译器 + make | `xcode-select --install` | `sudo apt-get install -y build-essential` | 推荐 **WSL2** 后按 Linux；或 MinGW-w64 + make |
| Node.js 18+ | `brew install node` 或 `nvm install 18` | `sudo apt-get install -y nodejs npm` | https://nodejs.org |
| Python 3.8+ | 系统自带 | `sudo apt-get install -y python3` | Microsoft Store Python |

### 2) 构建 C++ 内核

```bash
make -j8          # 产物为 ./petal（thirdparty 已内置，无需下载）
./petal           # 无参数应输出用法说明
```

### 3) 安装并构建前端

```bash
cd lab/web
npm install
npm run build     # 产物在 lab/web/dist，由服务端静态托管
```

### 4) 启动服务端

```bash
python3 ./lab/petal-lab serve --port 8899
# 浏览器打开 http://localhost:8899
```

## 验证是否跑通

```bash
# 1) 内核可用
./petal
# 2) 服务端可用（返回 JSON 列表）
curl -s http://localhost:8899/api/batches
# 3) 用内置示例数据跑一个最小实验
./petal examples/weather.pmeta examples/weather.pdata -dmdl -x10 -lnb --json=/tmp/r.json
```

## 排错

- **`make` 报找不到编译器**：按上表安装 Xcode CLT / build-essential / WSL。
- **前端 `npm install` 慢或失败**：确认 Node ≥ 18；可换镜像 `npm config set registry https://registry.npmmirror.com`。
- **服务端启动后页面空白**：确认先执行过 `npm run build` 生成 `lab/web/dist`。
- **改了 C++ 没生效**：必须重新 `make`；改了 `lab/` 下的 Python 需**重启 server** 才生效。
