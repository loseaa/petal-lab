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

  const filtered = useMemo(() => {
    const ds = filters.dataset.toLowerCase();
    const lr = filters.learner.toLowerCase();
    return runs.filter((r) => {
      if (ds && !r.dataset.toLowerCase().includes(ds)) return false;
      if (filters.mode && r.mode !== filters.mode) return false;
      if (lr && !(r.learners || []).some((l) => l.toLowerCase().includes(lr))) return false;
      return true;
    });
  }, [runs, filters]);

  return (
    <div className="grid2" style={{ gridTemplateColumns: 'minmax(340px, 30%) 1fr' }}>
      <div className="card">
        <h2>实验历史</h2>
        <p className="sub">
          共 {runs.length} 次运行。每次 <code>petal-lab run</code> 都会自动记录在此。
        </p>

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
            用 <code>petal-lab run -- &lt;petal 参数&gt;</code> 跑一次实验。
          </div>
        )}

        {filtered.length > 0 && (
          <table className="tbl">
            <thead>
              <tr>
                <th>#</th>
                <th>数据集</th>
                <th>学习器</th>
                <th>时间 (UTC+8)</th>
              </tr>
            </thead>
            <tbody>
              {filtered.map((r) => (
                <tr
                  key={r.id}
                  className={`clickable ${selected?.id === r.id ? 'selected' : ''}`}
                  onClick={() => { setSelected(r); onSelectRun?.(r.id); }}
                >
                  <td>{r.id}</td>
                  <td title={r.dataset}>
                    {basename(r.dataset)}
                    <div className="muted" style={{ fontSize: 11 }}>{r.mode}</div>
                  </td>
                  <td style={{ fontSize: 12 }}>{(r.learners || []).join(', ') || '—'}</td>
                  <td style={{ fontSize: 11 }} className="muted">
                    {r.created_at.slice(5, 19).replace('T', ' ')}
                  </td>
                </tr>
              ))}
            </tbody>
          </table>
        )}
      </div>

      <div>
        {selected ? (
          <RunDetail key={selected.id} run={selected} />
        ) : (
          <div className="card">
            <div className="empty">从左侧选择一次运行以查看图表。</div>
          </div>
        )}
      </div>
    </div>
  );
}
