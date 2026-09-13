import { useEffect, useMemo, useState } from 'react';
import { api } from '../api.js';
import Chart from '../components/Chart.jsx';
import StatusBadge from '../components/StatusBadge.jsx';
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
  const [progress, setProgress] = useState(null);

  useEffect(() => {
    let cancelled = false;
    let timer;
    let first = true;
    const load = () => {
      if (first) setLoading(true);
      Promise.all([
        api.compare({ batch_id: batch.id, metric }),
        api.runs({ batch_id: batch.id, limit: 1000 }),
        api.batchProgress(batch.id),
      ])
        .then(([c, r, p]) => {
          if (cancelled) return;
          setData(c);
          setRuns(r.runs || []);
          setProgress(p);
          setError(null);
        })
        .catch((e) => !cancelled && setError(e.message))
        .finally(() => {
          if (first) {
            setLoading(false);
            first = false;
          }
        });
    };
    load();
    // 批实验任务是异步跑的，定时刷新以实时显示逐渐完成的运行
    timer = setInterval(load, 3000);
    return () => {
      cancelled = true;
      clearInterval(timer);
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

  const metricLabel = METRICS.find((m) => m.id === metric)?.label || metric;
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

      {progress && (progress.total > 0 || runs.length > 0 || progress.failed > 0) && (() => {
        const total = progress.total || runs.length;
        const done = progress.done ?? runs.length;
        const running = progress.running || 0;
        const pending = progress.pending || 0;
        const failed = progress.failed || 0;
        const percent = progress.percent ?? 100;
        const active = (running + pending) > 0;
        const allFailed = failed > 0 && done === 0;
        return (
          <div className="card batch-progress">
            <div className="bp-head">
              <strong>{active ? '批处理进行中…' : allFailed ? '批处理失败' : '批处理已完成'}</strong>
              <span className="muted">
                {done} / {total} 次运行成功
                {active && ` · 运行中 ${running}${pending ? ` · 排队 ${pending}` : ''}`}
                {failed > 0 && ` · 失败 ${failed}`}
              </span>
            </div>
            <div className="bp-track">
              <div className={`bp-bar ${active ? 'live' : allFailed ? 'fail' : 'done'}`} style={{ width: `${percent}%` }} />
            </div>
          </div>
        );
      })()}

      {progress?.tasks?.length > 0 && (
        <div className="card task-states">
          <h2>任务状态（状态机）</h2>
          <div className="sm-legend">
            {['pending', 'running', 'done', 'failed', 'cancelled'].map((s) => (
              <span key={s} className="sm-step"><StatusBadge state={s} /></span>
            ))}
            <span className="sm-note">排队中 → 运行中 → 完成 / 失败 / 已取消</span>
          </div>
          <table className="tbl">
            <thead>
              <tr><th>数据集</th><th>算法</th><th>状态</th><th>说明</th></tr>
            </thead>
            <tbody>
              {progress.tasks.map((t) => (
                <tr key={t.id}>
                  <td>{basename(t.dataset) || '—'}</td>
                  <td>{t.learner || '—'}</td>
                  <td><StatusBadge state={t.status} /></td>
                  <td className="muted" style={{ fontSize: 12 }}>
                    {t.error
                      || (t.status === 'running' ? '执行中…' : t.status === 'pending' ? '等待并发槽' : '')}
                  </td>
                </tr>
              ))}
            </tbody>
          </table>
        </div>
      )}

      {data?.warning && (
        <div className="notice" style={{ marginBottom: 16 }}>
          {data.warning}
          {progress?.failed ? ` 本批次有 ${progress.failed} 个任务失败（结果未入库）。` : ''}
        </div>
      )}

      {(!data || data.datasets?.length === 0) && !loading && (
        <div className="empty">
          本批次没有任何可用的运行结果。
          {progress?.failed ? ` 其中 ${progress.failed} 个任务失败。` : ''}
          可在「批实验」页用相同设置重新运行该批次。
        </div>
      )}

      {!loading && !enough && (
        <div className="notice">
          这个批次目前只有 {fr?.n_datasets ?? 0} 个数据集，
          跨数据集统计需要<strong>至少 2 个</strong>（且每个算法在每个数据集上都有结果）。
          继续在网页上提交更多数据集到本批次。
        </div>
      )}

      {enough && fr && (
        <>
          <div className="card">
            <h2 style={{ fontSize: 19 }}>
              统计检验
              <span className="pill accent" style={{ fontFamily: 'var(--font-code)', fontSize: 11 }}>Friedman + Nemenyi</span>
            </h2>
            <p className="sub">
              先在每个数据集内部排名并求平均排名，再用 Friedman 整体检验与 Nemenyi 临界差异（CD）做两两比较。
              只要有两个算法的平均排名差超过 CD，即判定为存在显著差异（与下方 CD 图一致）。
            </p>
            <div className="chips mini">
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
              <div className={`chip ${data.pairwise_significant ? 'ok' : 'warn'}`}>
                <span className="k">结论</span>
                <span className="v">{data.pairwise_significant ? '存在显著差异' : '无显著差异'}</span>
              </div>
            </div>
            {!data.pairwise_significant && (
              <div className="notice" style={{ marginTop: 14, marginBottom: 0 }}>
                所有算法的平均排名两两差距均不超过临界差异 CD（{data.critical_difference?.toFixed(4)}）。
                {fr.n_datasets < 10 &&
                  ` 注意：仅 ${fr.n_datasets} 个数据集时检验功效很低，"不显著"往往只是样本不足，并不等于算法等价。`}
              </div>
            )}
            {data.pairwise?.length > 0 && (
              <div style={{ marginTop: 14 }}>
                <h3 style={{ fontSize: 14, marginBottom: 8 }}>两两差异（平均排名差 vs CD）</h3>
                <table className="tbl">
                  <thead>
                    <tr><th>算法 A</th><th>算法 B</th><th>排名差</th><th>判定</th></tr>
                  </thead>
                  <tbody>
                    {data.pairwise.map((p, i) => (
                      <tr key={i}>
                        <td>{p.learner_a}</td>
                        <td>{p.learner_b}</td>
                        <td>{p.rank_gap.toFixed(3)}</td>
                        <td>
                          {p.significant
                            ? <span style={{ color: 'var(--danger)', fontWeight: 600 }}>差异显著</span>
                            : <span className="muted">不显著</span>}
                        </td>
                      </tr>
                    ))}
                  </tbody>
                </table>
              </div>
            )}
          </div>

          {cd && (
            <Chart
              plain
              title="临界差异图（Critical Difference）"
              description="点表示算法的平均排名，越靠左越好；粗横线连接的算法之间差异不显著（排名差小于 CD）。"
              spec={cdDiagramSpec({ k: data.learners.length, points: cd.points, bars: cd.bars })}
            />
          )}

          {data.ranks?.length > 0 && (
            <Chart
              plain
              title="平均排名"
              description="在每个数据集内排名后取平均，1 为最优。"
              spec={rankSpec()}
              data={rankChartData}
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

      {heat.length > 0 && (
        <Chart
          plain
          title={`数据集 × 算法（${metricLabel}）`}
          description="颜色越浅越好（0-1 损失越小越浅）。空缺格子表示该组合暂无结果——任务失败或所选指标在该数据集上缺失。"
          spec={matrixHeatmapSpec()}
          data={heatChartData}
        />
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
