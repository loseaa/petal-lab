import { useEffect, useMemo, useState } from 'react';
import { api } from '../api.js';
import RunDetail from './RunDetail.jsx';

function basename(p) {
  return String(p || '').split(/[\\/]/).pop() || p;
}

export default function RunsView({ focusRunId, onSelectRun }) {
  const [runs, setRuns] = useState([]);
  const [loading, setLoading] = useState(true);
  const [error, setError] = useState(null);
  const [filters, setFilters] = useState({ dataset: '', learner: '', mode: '' });
  const [scope, setScope] = useState('all'); // all | single | batch
  const [selected, setSelected] = useState(null);

  useEffect(() => {
    let cancelled = false;
    setLoading(true);
    api
      .runs({ limit: 300 })
      .then((d) => {
        if (!cancelled) {
          setRuns(d.runs || []);
          setError(null);
        }
      })
      .catch((e) => !cancelled && setError(e.message))
      .finally(() => !cancelled && setLoading(false));
    return () => {
      cancelled = true;
    };
  }, []);

  // Auto-select the run the user jumped to from a finished job or a deep link.
  useEffect(() => {
    if (!focusRunId || !runs.length) return;
    const target = runs.find((r) => r.id === focusRunId);
    if (target) setSelected(target);
  }, [focusRunId, runs]);

  const counts = useMemo(() => {
    let single = 0;
    let batch = 0;
    for (const r of runs) (r.batch_id == null ? single++ : batch++);
    return { single, batch };
  }, [runs]);

  const filtered = useMemo(() => {
    const ds = filters.dataset.toLowerCase();
    const lr = filters.learner.toLowerCase();
    return runs.filter((r) => {
      if (scope === 'single' && r.batch_id != null) return false;
      if (scope === 'batch' && r.batch_id == null) return false;
      if (ds && !r.dataset.toLowerCase().includes(ds)) return false;
      if (filters.mode && r.mode !== filters.mode) return false;
      if (lr && !(r.learners || []).some((l) => l.toLowerCase().includes(lr))) return false;
      return true;
    });
  }, [runs, filters, scope]);

  return (
    <div className="grid2" style={{ gridTemplateColumns: 'minmax(340px, 30%) 1fr' }}>
      <div className="card">
        <h2>实验历史</h2>
        <p className="sub">
          共 {runs.length} 次运行。每次运行都会自动记录在此。
        </p>

        <div className="toolbar">
          <div className="seg">
            <button className={scope === 'all' ? 'on' : ''} onClick={() => setScope('all')}>
              全部 {runs.length}
            </button>
            <button className={scope === 'single' ? 'on' : ''} onClick={() => setScope('single')}>
              单次实验 {counts.single}
            </button>
            <button className={scope === 'batch' ? 'on' : ''} onClick={() => setScope('batch')}>
              批实验 {counts.batch}
            </button>
          </div>
        </div>

        <div className="toolbar">
          <input
            placeholder="数据集筛选"
            value={filters.dataset}
            onChange={(e) => setFilters((f) => ({ ...f, dataset: e.target.value }))}
            style={{ width: 130 }}
          />
          <input
            placeholder="学习器筛选"
            value={filters.learner}
            onChange={(e) => setFilters((f) => ({ ...f, learner: e.target.value }))}
            style={{ width: 120 }}
          />
          <select
            value={filters.mode}
            onChange={(e) => setFilters((f) => ({ ...f, mode: e.target.value }))}
          >
            <option value="">全部模式</option>
            <option value="xval">交叉验证</option>
            <option value="learning-curves">学习曲线</option>
          </select>
        </div>

        {error && <div className="error">{error}</div>}
        {loading && <div className="empty">加载中…</div>}
        {!loading && !filtered.length && (
          <div className="empty">
            没有匹配的记录。
            <br />
            用 <code>petal &lt;数据集.pmeta&gt; &lt;数据集.pdata&gt; ...</code> 跑一次实验，结果自动入库。
          </div>
        )}

        {filtered.length > 0 && (
          <div className="runlist">
            <div className="run-head">
              <div className="c-id"><span className="h-text">#</span></div>
              <div className="c-ds"><span className="h-text">数据集</span></div>
              <div className="c-algo"><span className="h-text">学习器</span></div>
              <div className="c-time"><span className="h-text">时间</span></div>
            </div>
            {filtered.map((r) => (
              <div
                key={r.id}
                className={`run-row ${selected?.id === r.id ? 'active' : ''}`}
                onClick={() => { setSelected(r); onSelectRun?.(r.id); }}
              >
                <div className="c-id">#{r.id}</div>
                <div className="c-ds">
                  <div>{basename(r.dataset)}</div>
                  <div className="muted" style={{ fontSize: 10.5 }}>
                    {r.batch_id != null ? `批次 #${r.batch_id}` : r.mode}
                  </div>
                </div>
                <div className="c-algo">
                  {(r.learners || []).map((l) => (
                    <span key={l} className="pill slate">{l}</span>
                  ))}
                </div>
                <div className="c-time">{r.created_at.slice(5, 19).replace('T', ' ')}</div>
              </div>
            ))}
          </div>
        )}
      </div>

      <div>
        {selected ? (
          <div key={selected.id} className="run-detail">
            <RunDetail run={selected} />
          </div>
        ) : (
          <div className="card">
            <div className="empty">从左侧选择一次运行以查看图表。</div>
          </div>
        )}
      </div>
    </div>
  );
}
