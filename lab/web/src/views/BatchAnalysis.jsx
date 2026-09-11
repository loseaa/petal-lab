import { useEffect, useMemo, useState } from 'react';
import { api } from '../api.js';
import Chart from '../components/Chart.jsx';
import RunDetail from './RunDetail.jsx';
import { rankSpec, cdDiagramSpec, matrixHeatmapSpec } from '../charts/specs.js';

const METRICS = [
  { id: '0-1_loss', label: '0-1 损失（越小越好）' },
  { id: 'rmse', label: 'RMSE（越小越好）' },
  { id: 'log_loss', label: '对数损失（越小越好）' },
  { id: 'auc', label: 'AUC（越大越好）' },
];

function basename(p) {
  return String(p || '').split(/[\\/]/).pop() || p;
}

export default function BatchAnalysis({ batch, onBack }) {
  const [metric, setMetric] = useState('0-1_loss');
  const [data, setData] = useState(null);
  const [runs, setRuns] = useState([]);
  const [loading, setLoading] = useState(true);
  const [error, setError] = useState(null);
  const [openRun, setOpenRun] = useState(null);

  useEffect(() => {
    let cancelled = false;
    setLoading(true);
    Promise.all([
      api.compare({ batch_id: batch.id, metric }),
      api.runs({ batch_id: batch.id, limit: 1000 }),
    ])
      .then(([c, r]) => {
        if (cancelled) return;
        setData(c);
        setRuns(r.runs || []);
        setError(null);
      })
      .catch((e) => !cancelled && setError(e.message))
      .finally(() => !cancelled && setLoading(false));
    return () => {
      cancelled = true;
    };
  }, [batch.id, metric]);

  // Memoised so react-vega does not rebuild the chart on every render.
  const rankChartData = useMemo(() => ({ values: data?.ranks ?? [] }), [data]);

  const heat = useMemo(() => {
    if (!data?.matrix) return [];
    const out = [];
    data.matrix.forEach((row, i) => {
      row.forEach((v, j) => {
        if (v !== null && v !== undefined) {
          out.push({ dataset: basename(data.datasets[i]), learner: data.learners[j], value: v });
        }
      });
    });
    return out;
  }, [data]);

  const heatChartData = useMemo(() => ({ values: heat }), [heat]);

  const cd = useMemo(() => {
    if (!data?.ranks) return null;
    // Two rows: points sit above, bars below, so bars don't run through points.
    const POINT_ROW = '算法排名';
    const BAR_ROW = '无显著差异';
    const points = data.ranks.map((r) => ({ learner: r.learner, rank: r.rank, y: POINT_ROW }));
    const bars = (data.cd_groups || []).map((g) => {
      const rs = g.map((i) => data.ranks[i].rank);
      return { x: Math.min(...rs), x2: Math.max(...rs), y: BAR_ROW };
    });
    return { points, bars };
  }, [data]);

  const fr = data?.friedman;
  const enough = (fr?.n_datasets ?? 0) >= 2;

  if (openRun) {
    return (
      <>
        <div className="page-head">
          <button className="btn ghost" onClick={() => setOpenRun(null)}>
            ← 返回批次「{batch.name}」
          </button>
        </div>
        <RunDetail run={openRun} key={openRun.id} />
      </>
    );
  }

  return (
    <>
      <div className="page-head">
        <button className="btn ghost" onClick={onBack} style={{ marginBottom: 6 }}>
          ← 全部批次
        </button>
        <h1>{batch.name}</h1>
        <p>
          {batch.description || '跨数据集对比实验'}
          {loading && ' · 计算中…'}
        </p>
      </div>

      {error && <div className="error">{error}</div>}

      <div className="toolbar">
        <span className="muted" style={{ fontSize: 12.5 }}>比较指标</span>
        <select value={metric} onChange={(e) => setMetric(e.target.value)}>
          {METRICS.map((m) => (
            <option key={m.id} value={m.id}>{m.label}</option>
          ))}
        </select>
      </div>

      {!loading && !enough && (
        <div className="notice">
          这个批次目前只有 {fr?.n_datasets ?? 0} 个数据集，
          跨数据集统计需要<strong>至少 2 个</strong>（且每个算法在每个数据集上都有结果）。
          继续用 <span className="mono">petal-lab run --batch {batch.name}</span> 添加更多数据集。
        </div>
      )}

      {enough && fr && (
        <>
          <div className="card">
            <h2>
              统计检验
              <span className="tag">Friedman + Nemenyi</span>
            </h2>
            <p className="sub">
              先把每个数据集内部的算法排名求平均，再用 Friedman 检验判断整体是否存在差异。
            </p>
            <div className="chips">
              <div className="chip">
                <span className="k">数据集 N</span>
                <span className="v">{fr.n_datasets}</span>
              </div>
              <div className="chip">
                <span className="k">算法 k</span>
                <span className="v">{fr.k}</span>
              </div>
              <div className="chip">
                <span className="k">Friedman χ²</span>
                <span className="v">{fr.statistic?.toFixed(4) ?? '—'}</span>
              </div>
              <div className="chip">
                <span className="k">临界值 α=0.05</span>
                <span className="v">{fr.critical_value?.toFixed(4) ?? '—'}</span>
              </div>
              <div className="chip">
                <span className="k">临界差异 CD</span>
                <span className="v">{data.critical_difference?.toFixed(4) ?? '—'}</span>
              </div>
              <div className={`chip ${fr.significant ? 'ok' : 'warn'}`}>
                <span className="k">结论</span>
                <span className="v">{fr.significant ? '存在显著差异' : '无显著差异'}</span>
              </div>
            </div>
            {!fr.significant && (
              <div className="notice" style={{ marginTop: 14, marginBottom: 0 }}>
                χ² = {fr.statistic?.toFixed(4)} 未超过临界值 {fr.critical_value?.toFixed(4)}，
                因此不能认为这些算法有显著差异。
                {fr.n_datasets < 10 &&
                  ` 注意：只有 ${fr.n_datasets} 个数据集时检验功效很低，
                   "不显著"往往只是样本不足，并不等于算法等价。`}
              </div>
            )}
          </div>

          {cd && (
            <Chart
              title="临界差异图（Critical Difference）"
              description="点表示算法的平均排名，越靠左越好；粗横线连接的算法之间差异不显著（排名差小于 CD）。"
              spec={cdDiagramSpec({ k: data.learners.length, points: cd.points, bars: cd.bars })}
            />
          )}

          {data.ranks?.length > 0 && (
            <Chart
              title="平均排名"
              description="在每个数据集内排名后取平均，1 为最优。"
              spec={rankSpec()}
              data={rankChartData}
            />
          )}

          {heat.length > 0 && (
            <Chart
              title="数据集 × 算法"
              description="用于发现「某算法在特定数据集上失效」——平均排名看不出这类问题。"
              spec={matrixHeatmapSpec()}
              data={heatChartData}
            />
          )}

          {data.win_draw_loss?.pairs?.length > 0 && (
            <div className="card">
              <h2>胜负平</h2>
              <p className="sub">两两对比在各数据集上的胜负计数。</p>
              <table className="tbl">
                <thead>
                  <tr>
                    <th>算法 A</th>
                    <th>算法 B</th>
                    <th>A 胜</th>
                    <th>平</th>
                    <th>A 负</th>
                  </tr>
                </thead>
                <tbody>
                  {data.win_draw_loss.pairs.map((p, i) => (
                    <tr key={i}>
                      <td>{data.learners[p.a]}</td>
                      <td>{data.learners[p.b]}</td>
                      <td><span className="pill green">{p.win}</span></td>
                      <td><span className="pill slate">{p.draw}</span></td>
                      <td><span className="pill amber">{p.loss}</span></td>
                    </tr>
                  ))}
                </tbody>
              </table>
            </div>
          )}
        </>
      )}

      <div className="card">
        <h2>批次内的运行（{runs.length}）</h2>
        <p className="sub">点击任意一次运行，可以查看它的学习曲线、ROC 等细节。</p>
        <table className="tbl">
          <thead>
            <tr>
              <th>#</th>
              <th>数据集</th>
              <th>算法</th>
              <th>模式</th>
              <th>汇总指标</th>
            </tr>
          </thead>
          <tbody>
            {runs.map((r) => (
              <tr
                key={r.id}
                className="clickable"
                onClick={() => setOpenRun(r)}
              >
                <td className="muted">{r.id}</td>
                <td>{basename(r.dataset)}</td>
                <td>
                  <span className="pill slate">{(r.learners || []).join(', ') || '—'}</span>
                </td>
                <td className="muted" style={{ fontSize: 12 }}>{r.mode}</td>
                <td className="muted" style={{ fontSize: 12 }}>点击查看 →</td>
              </tr>
            ))}
          </tbody>
        </table>
      </div>
    </>
  );
}
