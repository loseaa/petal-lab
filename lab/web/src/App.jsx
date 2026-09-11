import { useEffect, useState } from 'react';
import Overview from './views/Overview.jsx';
import RunsView from './views/RunsView.jsx';
import BatchesView from './views/BatchesView.jsx';
import RunExperiment from './views/RunExperiment.jsx';
import { api } from './api.js';

/**
 * Two different things are being managed, so the navigation keeps them apart:
 *
 *   单次运行 (Runs)    one data set × one learner  → 看这一次的曲线/ROC/混淆矩阵
 *   批实验 (Batches)   N data sets × M learners    → 看跨数据集的排名与显著性
 *
 * They used to be mixed together behind a single list, which made it unclear
 * whether a row was one experiment or part of a comparison.
 */
const PAGES = [
  { id: 'overview', label: '总览', icon: '◱' },
  { id: 'run', label: '运行实验', icon: '▶' },
  { id: 'batches', label: '批实验', icon: '▦' },
  { id: 'runs', label: '单次运行', icon: '◈' },
];

export default function App() {
  const [page, setPage] = useState('overview');
  const [counts, setCounts] = useState({ runs: 0, batches: 0 });
  // Set when the user jumps from a finished job to its stored result.
  const [focusRunId, setFocusRunId] = useState(null);
  // Jump to the single-run detail view for a specific run id (used by cards
  // and tables across pages that want to deep-link into a run).
  const openRun = (id) => {
    setFocusRunId(id);
    setPage('runs');
  };

  useEffect(() => {
    let cancelled = false;
    Promise.all([api.runs({ limit: 500 }), api.batches()])
      .then(([r, b]) => {
        if (!cancelled) setCounts({ runs: r.runs.length, batches: b.batches.length });
      })
      .catch(() => {});
    return () => {
      cancelled = true;
    };
  }, [page]);

  return (
    <div className="layout">
      <aside className="sidebar">
        <div className="brand">
          <div className="mark">P</div>
          <div>
            <div className="name">petal-lab</div>
            <div className="ver">实验管理与可视化</div>
          </div>
        </div>

        <div className="section">分析</div>
        <nav>
          {PAGES.map((p) => (
            <button
              key={p.id}
              className={page === p.id ? 'active' : ''}
              onClick={() => setPage(p.id)}
            >
              <span className="ico">{p.icon}</span>
              {p.label}
              {p.id === 'runs' && counts.runs > 0 && (
                <span className="badge">{counts.runs}</span>
              )}
              {p.id === 'batches' && counts.batches > 0 && (
                <span className="badge">{counts.batches}</span>
              )}
            </button>
          ))}
        </nav>

        <div className="section">导出</div>
        <div style={{ padding: '0 10px', fontSize: 11.5, color: '#a5b4fc', lineHeight: 1.6 }}>
          每张图右上角可导出 SVG（矢量，适合放进论文）
          或 PNG（约 300 dpi）。
        </div>

        <div className="foot">
          数据存于本地 petal.db
          <br />
          服务仅绑定 127.0.0.1
        </div>
      </aside>

      <main className="content">
        {page === 'overview' && <Overview onNavigate={setPage} onOpenRun={openRun} />}
        {page === 'run' && (
          <RunExperiment
            onViewRun={(id) => {
              setFocusRunId(id);
              setPage('runs');
            }}
          />
        )}
        {page === 'runs' && <RunsView focusRunId={focusRunId} />}
        {page === 'batches' && <BatchesView onOpenRun={openRun} />}
      </main>
    </div>
  );
}
