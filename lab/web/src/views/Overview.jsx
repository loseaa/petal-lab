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
    Promise.all([api.runs({ limit: 500 }), api.batches(), api.datasets()])
      .then(([runs, batches, ds]) => {
        if (!cancelled) setData({ runs: runs.runs, batches: batches.batches, datasets: ds.datasets });
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

  const { runs, batches, datasets } = data;
  const learners = new Set();
  runs.forEach((r) => (r.learners || []).forEach((l) => learners.add(l)));
  const recent = runs.slice(0, 8);

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

          <div className="card" style={{ margin: '0 0 12px', boxShadow: 'none', background: 'var(--panel-2)' }}>
            <h2 style={{ fontSize: 13.5 }}>◈ 单次运行</h2>
            <p className="sub" style={{ marginBottom: 0 }}>
              一个数据集 × 一个算法。看的是<strong>这一次</strong>的学习曲线、折间分布、
              ROC/PR、混淆矩阵。
              <br />
              <span className="mono">petal-lab run -- data.pm data.pd -x10 -lnb</span>
            </p>
          </div>

          <div className="card" style={{ margin: 0, boxShadow: 'none', background: 'var(--panel-2)' }}>
            <h2 style={{ fontSize: 13.5 }}>▦ 批实验</h2>
            <p className="sub" style={{ marginBottom: 0 }}>
              多个数据集 × 多个算法。看的是<strong>整体</strong>谁更好：平均排名、
              临界差异图、胜负平、数据集×算法热力图。
              <br />
              <span className="mono">petal-lab batch run --name exp1 --learners nb,aode,tan</span>
            </p>
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
            <table className="tbl">
              <thead>
                <tr>
                  <th>#</th>
                  <th>数据集</th>
                  <th>算法</th>
                  <th>批次</th>
                </tr>
              </thead>
              <tbody>
                {recent.map((r) => (
                  <tr
                    key={r.id}
                    className="clickable"
                    onClick={() => onOpenRun?.(r.id)}
                    title="查看运行详情"
                  >
                    <td className="muted">{r.id}</td>
                    <td>{basename(r.dataset)}</td>
                    <td>
                      <span className="pill slate">{(r.learners || []).join(', ') || '—'}</span>
                    </td>
                    <td>
                      {r.batch_name ? (
                        <span className="pill">{r.batch_name}</span>
                      ) : (
                        <span className="muted">—</span>
                      )}
                    </td>
                  </tr>
                ))}
              </tbody>
            </table>
          )}
        </div>
      </div>
    </>
  );
}
