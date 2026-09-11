import { useEffect, useState } from 'react';
import Overview from './views/Overview.jsx';
import RunsView from './views/RunsView.jsx';
import BatchesView from './views/BatchesView.jsx';
import RunExperiment from './views/RunExperiment.jsx';
import { api } from './api.js';

const PAGES = [
  { id: 'overview', label: '总览', icon: '◱' },
  { id: 'run', label: '运行实验', icon: '▶' },
  { id: 'batches', label: '批实验', icon: '▦' },
  { id: 'runs', label: '单次运行', icon: '◈' },
];

/**
 * The app used to live entirely in component state with no URL, so a result
 * could only be reached by clicking through the UI. We now mirror the active
 * view + selection into the URL hash (#/runs/123, #/batches/exp1) so a link can
 * be opened / shared / reloaded directly — that's what lets an agent deep-link
 * straight to a specific experiment's result page.
 */
function parseHash() {
  const raw = window.location.hash.replace(/^#\/?/, '').trim();
  if (!raw) return { page: 'overview', focusRunId: null, focusBatch: null };
  const [seg, param] = raw.split('/');
  if (seg === 'run') return { page: 'run', focusRunId: null, focusBatch: null };
  if (seg === 'batches') {
    return { page: 'batches', focusRunId: null, focusBatch: param || null };
  }
  if (seg === 'runs') {
    const id = param ? Number(param) : NaN;
    return { page: 'runs', focusRunId: Number.isNaN(id) ? null : id, focusBatch: null };
  }
  return { page: 'overview', focusRunId: null, focusBatch: null };
}

export default function App() {
  const initial = parseHash();
  const [page, setPage] = useState(initial.page);
  const [counts, setCounts] = useState({ runs: 0, batches: 0 });
  // focusRunId: integer run id (deep link #/runs/<id>)
  // focusBatch: batch id or name (deep link #/batches/<id|name>)
  const [focusRunId, setFocusRunId] = useState(initial.focusRunId);
  const [focusBatch, setFocusBatch] = useState(initial.focusBatch);

  // Mirror the current view + selection into the URL hash.
  useEffect(() => {
    let hash = '#/overview';
    if (page === 'run') hash = '#/run';
    else if (page === 'batches') hash = focusBatch ? `#/batches/${focusBatch}` : '#/batches';
    else if (page === 'runs') hash = focusRunId != null ? `#/runs/${focusRunId}` : '#/runs';
    if (window.location.hash !== hash) window.location.hash = hash;
  }, [page, focusRunId, focusBatch]);

  // Follow manual hash edits (pasted link, back/forward buttons).
  useEffect(() => {
    const onHash = () => {
      const p = parseHash();
      setPage(p.page);
      setFocusRunId(p.focusRunId);
      setFocusBatch(p.focusBatch);
    };
    window.addEventListener('hashchange', onHash);
    return () => window.removeEventListener('hashchange', onHash);
  }, []);

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
        {page === 'runs' && (
          <RunsView focusRunId={focusRunId} onSelectRun={setFocusRunId} />
        )}
        {page === 'batches' && (
          <BatchesView
            onOpenRun={openRun}
            focusBatch={focusBatch}
            onOpenBatch={setFocusBatch}
          />
        )}
      </main>
    </div>
  );
}
