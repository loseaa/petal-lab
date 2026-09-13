import { Fragment, useEffect, useState } from 'react';
import Overview from './views/Overview.jsx';
import RunsView from './views/RunsView.jsx';
import BatchesView from './views/BatchesView.jsx';
import RunExperiment from './views/RunExperiment.jsx';
import DataAnalysisView from './views/DataAnalysisView.jsx';
import Icon from './components/Icon.jsx';
import { api } from './api.js';

const PAGES = [
  { id: 'overview', label: '总览', icon: 'overview' },
  { id: 'run', label: '单次实验', icon: 'run' },
  { id: 'batches', label: '批实验', icon: 'batches' },
  { id: 'runs', label: '运行记录', icon: 'runs' },
  { id: 'data', label: '数据分析', icon: 'data' },
];

// 面包屑：把"实验发起"与"结果查看"的层级摆清楚
const CRUMBS = {
  overview: '总览',
  run: '实验 / 单次实验',
  batches: '实验 / 批实验',
  runs: '结果 / 运行记录',
  data: '数据 / 数据分析',
};

// 侧边栏分组：把"发起"与"查看结果"分开，避免菜单扁平混乱
const NAV_GROUPS = [
  { name: '概览', ids: ['overview'] },
  { name: '数据', ids: ['data'] },
  { name: '实验', ids: ['run', 'batches'] },
  { name: '结果', ids: ['runs'] },
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
  if (seg === 'data') return { page: 'data', focusRunId: null, focusBatch: null };
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
    else if (page === 'data') hash = '#/data';
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

        {NAV_GROUPS.map((g) => (
          <Fragment key={g.name}>
            <div className="section">{g.name}</div>
            <nav>
              {PAGES.filter((p) => g.ids.includes(p.id)).map((p) => (
                <button
                  key={p.id}
                  className={page === p.id ? 'active' : ''}
                  onClick={() => setPage(p.id)}
                >
                  <span className="ico"><Icon name={p.icon} /></span>
                  <span className="label">{p.label}</span>
                  {p.id === 'runs' && counts.runs > 0 && (
                    <span className="badge">{counts.runs}</span>
                  )}
                  {p.id === 'batches' && counts.batches > 0 && (
                    <span className="badge">{counts.batches}</span>
                  )}
                </button>
              ))}
            </nav>
          </Fragment>
        ))}

        <div className="section">导出</div>
        <div className="hint">
          每张图右上角可导出 SVG（矢量，适合放进论文）
          或 PNG（约 300 dpi）。
        </div>

        <div className="foot">
          数据存于本地 petal.db
          <br />
          服务仅绑定 127.0.0.1
        </div>
      </aside>

      <main className="content" key={page}>
        <div className="crumb">{CRUMBS[page]}</div>
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
        {page === 'data' && <DataAnalysisView />}
      </main>
    </div>
  );
}
