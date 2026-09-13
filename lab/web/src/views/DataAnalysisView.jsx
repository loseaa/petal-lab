import { useEffect, useMemo, useState } from 'react';
import { api } from '../api.js';

const COLS = [
  { id: 'name', label: '数据集', num: false },
  { id: 'n_records', label: '记录行数', num: true },
  { id: 'n_attributes', label: '属性数', num: true },
  { id: 'n_predictors', label: '其他变量(X)', num: true },
  { id: 'n_class_attrs', label: '类属性数', num: true },
  { id: 'n_classes', label: '类标签数', num: true },
];

export default function DataAnalysisView() {
  const [list, setList] = useState([]);
  const [loading, setLoading] = useState(true);
  const [error, setError] = useState(null);
  const [sort, setSort] = useState('name');
  const [asc, setAsc] = useState(true);
  const [open, setOpen] = useState(null);
  const [detail, setDetail] = useState(null);
  const [detailLoading, setDetailLoading] = useState(false);

  useEffect(() => {
    let cancelled = false;
    setLoading(true);
    api.datasetAnalysis()
      .then((r) => !cancelled && setList(r.datasets || []))
      .catch((e) => !cancelled && setError(e.message))
      .finally(() => !cancelled && setLoading(false));
    return () => {
      cancelled = true;
    };
  }, []);

  const sorted = useMemo(() => {
    const arr = [...list];
    const col = COLS.find((c) => c.id === sort);
    arr.sort((a, b) => {
      const c = col.num ? (a[sort] || 0) - (b[sort] || 0) : String(a.name).localeCompare(String(b.name));
      return asc ? c : -c;
    });
    return arr;
  }, [list, sort, asc]);

  function openDetail(name) {
    if (open === name) {
      setOpen(null);
      setDetail(null);
      return;
    }
    setOpen(name);
    setDetail(null);
    setDetailLoading(true);
    api.datasetDetail(name)
      .then((d) => setDetail(d))
      .catch((e) => setError(e.message))
      .finally(() => setDetailLoading(false));
  }

  const dist = useMemo(() => {
    if (!detail || !detail.class_distribution) return [];
    const entries = Object.entries(detail.class_distribution);
    const max = Math.max(1, ...entries.map(([, v]) => v));
    return entries
      .map(([label, count]) => ({
        label,
        count,
        pct: detail.n_records ? (count / detail.n_records) * 100 : 0,
        w: (count / max) * 100,
      }))
      .sort((a, b) => b.count - a.count);
  }, [detail]);

  return (
    <div className="da-layout">
      <div className="da-col-left">
        <div className="page-head">
          <h1>数据分析</h1>
          <p>
            读取数据集时解析出的额外信息：记录行数、类属性数、其他变量数、类标签数，
            以及类标签分布等初步分析。<strong>点击左侧任意数据集行，右侧查看其具体分析</strong>。
          </p>
        </div>

        {error && <div className="error">{error}</div>}
        {loading ? (
          <div className="empty">加载中…（首次需逐文件统计行数，稍候）</div>
        ) : (
          <div className="card da-list">
            <div className="da-toolbar">
              <span className="muted" style={{ fontSize: 12 }}>点击表头排序；点数据集行查看右侧初步分析</span>
            </div>
            <div className="da-table">
              <div className="da-row da-head">
                {COLS.map((c) => (
                  <button
                    type="button"
                    key={c.id}
                    className={`da-th ${sort === c.id ? 'on' : ''}`}
                    onClick={() => {
                      if (sort === c.id) setAsc((v) => !v);
                      else {
                        setSort(c.id);
                        setAsc(c.num ? false : true);
                      }
                    }}
                  >
                    {c.label}
                    {sort === c.id && <span className="da-caret">{asc ? '▲' : '▼'}</span>}
                  </button>
                ))}
              </div>
              {sorted.map((d) => (
                <div
                  key={d.name}
                  className={`da-row ${open === d.name ? 'active' : ''}`}
                  onClick={() => openDetail(d.name)}
                >
                  <div className="da-td da-name" title={d.name}>{d.name}</div>
                  <div className="da-td num">{d.n_records?.toLocaleString()}</div>
                  <div className="da-td num">{d.n_attributes}</div>
                  <div className="da-td num">{d.n_predictors}</div>
                  <div className="da-td num">{d.n_class_attrs}</div>
                  <div className="da-td num">{d.n_classes}</div>
                </div>
              ))}
            </div>
          </div>
        )}
      </div>

      <div className="da-col-right">
        <div className="card da-detail">
          {!open && (
            <div className="da-placeholder">
              ← 从左侧选择一个数据集，查看它的类属性、其他变量、类标签数与类标签分布等初步分析。
            </div>
          )}
          {open && detailLoading && <div className="empty">加载数据集详情…</div>}
          {open && detail && !detailLoading && (
            <>
              <div className="da-detail-head">
                <h2>{detail.name}</h2>
                <button
                  type="button"
                  className="link-btn"
                  onClick={() => {
                    setOpen(null);
                    setDetail(null);
                  }}
                >
                  收起 ✕
                </button>
              </div>
              <div className="mono muted" style={{ fontSize: 12, wordBreak: 'break-all' }}>
                {detail.meta} · {detail.data}
              </div>

              <div className="da-stats">
                <div className="da-stat"><div className="v">{detail.n_records?.toLocaleString()}</div><div className="k">记录行数</div></div>
                <div className="da-stat"><div className="v">{detail.n_attributes}</div><div className="k">属性总数</div></div>
                <div className="da-stat"><div className="v">{detail.n_class_attrs}</div><div className="k">类属性数</div></div>
                <div className="da-stat"><div className="v">{detail.n_predictors}</div><div className="k">其他变量(X)</div></div>
                <div className="da-stat"><div className="v">{detail.n_classes}</div><div className="k">类标签数</div></div>
              </div>

              <div className="da-compo">
                <strong>属性构成：</strong>
                {(() => {
                  const num = (detail.attributes || []).filter((a) => a.type === 'numeric').length;
                  const cat = (detail.attributes || []).filter((a) => a.type === 'categorical').length;
                  return <span className="muted">数值型 {num} 个 · 类别型 {cat} 个</span>;
                })()}
                {detail.class_type === 'numeric' && (
                  <span className="tag" style={{ marginLeft: 8 }}>回归（连续类标签）</span>
                )}
              </div>

              <h3 style={{ marginTop: 16 }}>类标签分布（初步分析）</h3>
              {detail.class_distribution ? (
                <div className="dist">
                  {dist.map((x) => (
                    <div className="dist-row" key={x.label}>
                      <div className="dist-label" title={x.label}>{x.label}</div>
                      <div className="dist-track">
                        <div className="dist-fill" style={{ width: `${x.w}%` }} />
                      </div>
                      <div className="dist-num">{x.count.toLocaleString()} · {x.pct.toFixed(1)}%</div>
                    </div>
                  ))}
                </div>
              ) : (
                <div className="muted">
                  {detail.class_type === 'numeric'
                    ? '该数据集为回归任务（类标签是连续值），无离散类别分布。'
                    : '未解析到类别信息，无法绘制分布。'}
                </div>
              )}
            </>
          )}
        </div>
      </div>
    </div>
  );
}
