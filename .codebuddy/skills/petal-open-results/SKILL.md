---
name: petal-open-results
description: 启动 Petal 可视化看板并打开结果页面。当用户说"打开页面看结果""把实验结果展示出来""我想在浏览器看某次运行/某批次""启动可视化看板"时使用。封装：确认前端已构建、拉起（或复用）petal-lab serve、在浏览器/IDE 预览中打开并导航到对应的结果视图（总览 / 运行实验 / 单次运行 / 批实验），支持用 hash 深链一步直达某次运行或某批次。
---

# 打开 Petal 可视化看板看结果

## 用途

把"看实验结果"这一步自动化：确认前端产物 → 启动（或复用）本地服务 → 打开浏览器并定位到用户要的那一页（现已支持 URL 深链）。

## 何时使用

- 用户说"打开页面看结果""把结果展示出来""我想看看这次实验"
- 用户指名要看某个 run / 某个 batch 的可视化
- 需要确认服务是否在跑、前端是否最新

## 前置与依赖

- 前端产物：`lab/web/dist`。不存在或怀疑过旧时先 `cd lab/web && npm install && npm run build`。
- 服务入口：`./lab/petal-lab serve`（默认 `127.0.0.1:8899`）。petal 二进制由 `make -j8` 生成，serve 自动定位 `../../petal`；找不到用 `--petal` 指定。
- 数据库：`lab/petal.db`（SQLite，结果来源）。
- IDE 预览：用 `preview_url` 打开 `http://127.0.0.1:8899/`。

## 执行流程

1. **确认前端已构建**：检查 `lab/web/dist/index.html`。缺则 build。
2. **复用或拉起服务**：
   - 先确认 8899 是否已在监听（已有进程则复用，不要重复拉起）。
   - 未监听则在后台启动：`cd lab && nohup ./petal-lab serve > /tmp/petal-lab-serve.log 2>&1 &`，再看日志确认 `已启动: http://127.0.0.1:8899`。
3. **打开页面并定位目标视图**：前端支持 hash 路由，优先用 URL 直达，避免手动点击：
   - 总览 → `#/overview`
   - 运行实验 → `#/run`
   - 批实验列表 → `#/batches`；直接展开某批次 → `#/batches/<id 或 name>`（如 `#/batches/exp1`）
   - 单次运行列表 → `#/runs`；直接打开某次运行详情 → `#/runs/<id>`（如 `#/runs/123`）
   - 定位具体 run / batch：先用 `./lab/petal-lab list --limit 20` 或 API `GET /api/runs`、`GET /api/batches` 拿到 id / 批次名，再 `preview_url` 打开 `http://127.0.0.1:8899/#/runs/<id>` 之类即一步直达。
   - 若只想要首页：`preview_url` 打开 `http://127.0.0.1:8899/`。

## 深链直达（已内置）

前端已支持 hash 路由，无需先到首页再点击：
- `#/runs/<id>` 直接打开某次运行的详情（学习曲线 / 折间分布 / ROC·PR / 混淆矩阵）。
- `#/batches/<id 或 name>` 直接展开某批次的分析视图。
- 用户在页面内点击导航时，URL 也会同步更新，可复制分享、可刷新保持。

## 页面与图表能看什么

- 单次运行详情：指标 chips、学习曲线、折间分布（箱线）、ROC/PR（需 `--dump-predictions`）、混淆矩阵；每图可导出 SVG/PNG。
- 批实验：批次内每次运行列表，点开等价于单次详情；整批可横向对比。
- 总览：数据集统计、最近运行速览、快速开始。

## 常见故障

- **白屏 / 图表空白**：前端未 build 或版本过旧 → 重新 `npm run build` 并硬刷新（Cmd/Ctrl+Shift+R）。
- **8899 拒绝连接**：服务没起。后台拉起 serve 并看 `/tmp/petal-lab-serve.log`。
- **某图表为空**：那次运行没 `--dump-predictions`（ROC/PR/混淆矩阵依赖逐样本预测）。

## 关闭

服务常驻不自动退出；用户不再需要时 kill 对应进程即可（数据库保留）。
