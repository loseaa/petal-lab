import { useEffect, useMemo, useState } from 'react';
import { api } from '../api.js';
import BatchAnalysis from './BatchAnalysis.jsx';

function basename(p) {
  return String(p || '').split(/[\\/]/).pop() || p;
}

export default function BatchesView({ onOpenRun }) {
  const [batches, setBatches] = useState([]);
  const [runs, setRuns] = useState([]);
  const [loading, setLoading] = useState(true);
  const [error, setError] = useState(null);
  const [openId, setOpenId] = useState(null);

  useEffect(() => {
    let cancelled = false;
    setLoading(true);
    Promise.all([api.batches(), api.runs({ limit: 1000 })])
      .then(([b, r]) => {
        if (cancelled) return;
        setBatches(b.batches || []);
        setRuns(r.runs || []);
        setError(null);
      })
      .catch((e) => !cancelled && setError(e.message))
      .finally(() => !cancelled && setLoading(false));
    return () => {
      cancelled = true;
    };
  }, []);

  // A batch is only analysable across data sets, so show how big each one is.
  const stats = useMemo(() => {
    const m = new Map();
    for (const r of runs) {
      if (r.batch_id == null) continue;
      if (!m.has(r.batch_id)) m.set(r.batch_id, { datasets: new Set(), learners: new Set() });
      m.get(r.batch_id).datasets.add(r.dataset);
      (r.learners || []).forEach((l) => m.get(r.batch_id).learners.add(l));
    }
    return m;
  }, [runs]);

  if (openId != null) {
    const batch = batches.find((b) => b.id === openId);
    return (
      <BatchAnalysis batch={batch} onBack={() => setOpenId(null)} />
    );
  }

  return (
    <>
      <div className="page-head">
        <h1>批实验</h1>
        <p>
          一组「多个数据集 × 多个算法」的实验。用来回答<strong>整体上哪个算法更好</strong>，
          而不是单次运行的细节。
        </p>
      </div>

      {error && <div className="error">{error}</div>}
      {loading && <div className="empty">加载中…</div>}

      {!loading && batches.length === 0 && (
        <div className="empty">
          还没有批实验。
          <br />
          <br />
          <span className="mono">
            petal-lab batch run --name exp1 --datasets-dir data/ --learners nb,aode,tan
          </span>
          <br />
          <br />
          或者把已有运行归入一个批次：
          <br />
          <span className="mono">petal-lab run --batch exp1 -- &lt;petal 参数&gt;</span>
        </div>
      )}

      {batches.length > 0 && (
        <div className="card">
          <h2>全部批次</h2>
          <p className="sub">点击进入查看跨数据集统计。</p>
          <table className="tbl">
            <thead>
              <tr>
                <th>批次</th>
                <th>运行数</th>
                <th>数据集</th>
                <th>算法</th>
                <th>创建时间 (UTC+8)</th>
                <th />
              </tr>
            </thead>
            <tbody>
              {batches.map((b) => {
                const s = stats.get(b.id);
                const nDatasets = s ? s.datasets.size : 0;
                const nLearners = s ? s.learners.size : 0;
                return (
                  <tr
                    key={b.id}
                    className="clickable"
                    onClick={() => setOpenId(b.id)}
                  >
                    <td>
                      <strong>{b.name}</strong>
                      {b.description && (
                        <div className="muted" style={{ fontSize: 11.5 }}>{b.description}</div>
                      )}
                    </td>
                    <td>{b.run_count}</td>
                    <td>
                      <span className={`pill ${nDatasets >= 2 ? 'green' : 'amber'}`}>
                        {nDatasets}
                      </span>
                    </td>
                    <td>
                      <span className={`pill ${nLearners >= 2 ? 'cyan' : 'slate'}`}>
                        {nLearners}
                      </span>
                    </td>
                    <td className="muted" style={{ fontSize: 11.5 }}>
                      {b.created_at.slice(0, 19).replace('T', ' ')}
                    </td>
                    <td>
                      <button className="btn">分析 →</button>
                    </td>
                  </tr>
                );
              })}
            </tbody>
          </table>
          <div className="notice" style={{ marginTop: 14, marginBottom: 0 }}>
            跨数据集统计需要<strong>至少 2 个数据集</strong>且每个算法在每个数据集上都有结果；
            Friedman 检验要有统计效力通常还要更多（经验上 10 个以上）。
          </div>
        </div>
      )}

      {runs.some((r) => r.batch_id == null) && (
        <div className="card">
          <h2>未归入批次的运行</h2>
          <p className="sub">
            这些是单独跑的。要参与跨数据集比较，需要把它们归入一个批次。
          </p>
          <table className="tbl">
            <thead>
              <tr>
                <th>#</th>
                <th>数据集</th>
                <th>算法</th>
              </tr>
            </thead>
            <tbody>
              {runs
                .filter((r) => r.batch_id == null)
                .slice(0, 10)
                .map((r) => (
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
                  </tr>
                ))}
            </tbody>
          </table>
        </div>
      )}
    </>
  );
}
