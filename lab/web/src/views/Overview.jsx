import { useEffect, useState } from 'react';
import { api } from '../api.js';

function basename(p) {
  return String(p || '').split(/[\\/]/).pop() || p;
}

export default function Overview({ onNavigate, onOpenRun }) {
  const [data, setData] = useState(null);
  const [error, setError] = useState(null);

  useEffect(() => {
    let cancelled = false;
    Promise.all([api.runs({ limit: 500 }), api.batches(), api.datasets(), api.availableDatasets()])
      .then(([runs, batches, ds, avail]) => {
        if (!cancelled) setData({
          runs: runs.runs,
          batches: batches.batches,
          datasets: ds.datasets,      // 数据库里出现过的不同数据集（用于统计卡片）
          available: avail.datasets,  // 磁盘上可跑的数据集（用于动态拼命令的真实路径）
        });
      })
      .catch((e) => !cancelled && setError(e.message));
    return () => {
      cancelled = true;
    };
  }, []);

  if (error) {
    return (
      <>
        <div className="page-head"><h1>总览</h1></div>
        <div className="error">{error}</div>
      </>
    );
  }
  if (!data) {
    return (
      <>
        <div className="page-head"><h1>总览</h1></div>
        <div className="empty">加载中…</div>
      </>
    );
  }

  const { runs, batches, datasets, available } = data;
  const learners = new Set();
  runs.forEach((r) => (r.learners || []).forEach((l) => learners.add(l)));
  const recent = runs.slice(0, 6);

  // 命令示例用当前项目里真实存在的数据集路径动态拼，绝不写死。
  const exampleDs = available && available[0];
  const singleCmd = exampleDs
    ? `petal ${exampleDs.meta} ${exampleDs.data} -dmdl -v2 -x10 -lnb`
    : `petal <数据集.pmeta> <数据集.pdata> -dmdl -v2 -x10 -lnb`;
  const batchCmd = exampleDs
    ? `petal ${exampleDs.meta} ${exampleDs.data} -dmdl -v2 -x10 -lnb   # 每个数据集×算法各跑一次`
    : `petal <数据集.pmeta> <数据集.pdata> -dmdl -v2 -x10 -lnb   # 每个数据集×算法各跑一次`;

  return (
    <>
      <div className="page-head">
        <h1>总览</h1>
        <p>本地实验库的整体情况。</p>
      </div>

      <div className="grid3" style={{ marginBottom: 18 }}>
        <div className="stat">
          <div className="k">单次运行</div>
          <div className="v">{runs.length}</div>
          <div className="hint">一次 petal 执行 = 一条记录</div>
        </div>
        <div className="stat cyan">
          <div className="k">批实验</div>
          <div className="v">{batches.length}</div>
          <div className="hint">成组的对比实验</div>
        </div>
        <div className="stat green">
          <div className="k">数据集</div>
          <div className="v">{datasets.length}</div>
          <div className="hint">跨数据集比较至少需要 2 个</div>
        </div>
        <div className="stat amber">
          <div className="k">涉及算法</div>
          <div className="v">{learners.size}</div>
          <div className="hint">{[...learners].slice(0, 3).join('、') || '—'}</div>
        </div>
      </div>

      <div className="grid2">
        <div className="card" style={{ marginBottom: 0 }}>
          <h2>
            从这里开始
            <span className="tag">两种实验</span>
          </h2>
          <p className="sub">
            这两种实验关注的东西不同，所以分开管理：
          </p>

          <div className="card" style={{ margin: '0 0 12px', boxShadow: 'none', background: 'var(--panel-2)', borderRadius: 14 }}>
            <h2 style={{ fontSize: 16, fontFamily: 'var(--font-heading)', fontWeight: 500, display: 'flex', alignItems: 'center', gap: 8 }}>
              ◈ 单次运行
            </h2>
            <p className="sub" style={{ marginBottom: 10 }}>
              一个数据集 × 一个算法。看的是<strong>这一次</strong>的学习曲线、折间分布、
              ROC/PR、混淆矩阵。
            </p>
            <div className="cmd">{singleCmd}</div>
          </div>

          <div className="card" style={{ margin: 0, boxShadow: 'none', background: 'var(--panel-2)', borderRadius: 14 }}>
            <h2 style={{ fontSize: 16, fontFamily: 'var(--font-heading)', fontWeight: 500, display: 'flex', alignItems: 'center', gap: 8 }}>
              ▦ 批实验
            </h2>
            <p className="sub" style={{ marginBottom: 10 }}>
              多个数据集 × 多个算法。看的是<strong>整体</strong>谁更好：平均排名、
              临界差异图、胜负平、数据集×算法热力图。
            </p>
            <div className="cmd">{batchCmd}</div>
          </div>

          <div className="toolbar" style={{ marginTop: 14, marginBottom: 0 }}>
            <button className="btn primary" onClick={() => onNavigate('batches')}>
              查看批实验
            </button>
            <button className="btn" onClick={() => onNavigate('runs')}>
              查看单次运行
            </button>
          </div>
        </div>

        <div className="card" style={{ marginBottom: 0 }}>
          <h2>最近运行</h2>
          <p className="sub">最新的 {recent.length} 条记录。</p>
          {recent.length === 0 ? (
            <div className="empty" style={{ padding: 28 }}>
              还没有数据
            </div>
          ) : (
            <div className="runlist">
              <div className="run-head">
                <div className="run-accent" />
                <div className="c-idx"><span className="h-text">#</span></div>
                <div className="c-ds"><span className="h-text">数据集</span></div>
                <div className="c-algo"><span className="h-text">算法</span></div>
                <div className="c-batch"><span className="h-text">批次</span></div>
              </div>
              {recent.map((r, i) => (
                <div
                  key={r.id}
                  className={`run-row${i % 2 === 1 ? ' alt' : ''}`}
                  onClick={() => onOpenRun?.(r.id)}
                  title="查看运行详情"
                >
                  <div className="run-accent" />
                  <div className="c-idx">{r.id}</div>
                  <div className="c-ds">{basename(r.dataset)}</div>
                  <div className="c-algo">{(r.learners || []).join(', ') || '—'}</div>
                  <div className="c-batch">{r.batch_name || '—'}</div>
                </div>
              ))}
            </div>
          )}
        </div>
      </div>
    </>
  );
}
