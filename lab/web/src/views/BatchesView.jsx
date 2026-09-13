import { useEffect, useMemo, useState } from 'react';
import { api } from '../api.js';

const LEARNERS = ['nb', 'aode', 'tan', 'kdb', 'cbn', 'gnb', 'a2de', 'a3de', 'DTree', 'AdaBoost'];
const DISCRETISERS = [
  { id: '', label: '不离散化' },
  { id: 'mdl', label: 'MDL 离散化' },
  { id: 'mdl2', label: 'MDL 二值离散化' },
  { id: 'equal-frequency', label: '等频离散化' },
  { id: 'equal-depth', label: '等深离散化' },
];
// 数据集选择区的排序方式：除默认字母序外，支持按读取到的各项指标排序
const DS_SORTS = [
  { id: 'alpha', label: '字母 A–Z' },
  { id: 'n_records', label: '记录行数' },
  { id: 'n_classes', label: '类标签数' },
  { id: 'n_attributes', label: '属性数' },
  { id: 'n_predictors', label: '其他变量数' },
];
import BatchAnalysis from './BatchAnalysis.jsx';

function basename(p) {
  return String(p || '').split(/[\\/]/).pop() || p;
}

export default function BatchesView({ onOpenRun, focusBatch, onOpenBatch }) {
  const [batches, setBatches] = useState([]);
  const [runs, setRuns] = useState([]);
  const [loading, setLoading] = useState(true);
  const [error, setError] = useState(null);
  const [openId, setOpenId] = useState(null);

  // 批实验勾选式创建所需
  const [datasets, setDatasets] = useState([]);
  const [selDs, setSelDs] = useState(() => new Set());
  const [selLr, setSelLr] = useState(() => new Set(['nb']));
  const [dsQuery, setDsQuery] = useState('');
  const [batchName, setBatchName] = useState('');
  const [folds, setFolds] = useState(10);
  const [discretiser, setDiscretiser] = useState('mdl');
  const [submitting, setSubmitting] = useState(false);
  const [formErr, setFormErr] = useState(null);
  const [formResult, setFormResult] = useState(null);
  const [hasLast, setHasLast] = useState(false);
  // 数据集额外指标（行数/类标签/属性等），来自 /api/datasets/analysis，用于按指标排序
  const [metaMap, setMetaMap] = useState({});
  const [dsSort, setDsSort] = useState('alpha');
  const metaReady = Object.keys(metaMap).length > 0;

  // 记住上一次成功提交的批实验选择，供「恢复上次选择」使用
  useEffect(() => {
    try { setHasLast(Boolean(localStorage.getItem('petal-lab:lastBatch'))); } catch {}
  }, []);

  useEffect(() => {
    let cancelled = false;
    setLoading(true);
    Promise.all([api.batches(), api.runs({ limit: 1000 }), api.availableDatasets()])
      .then(([b, r, avail]) => {
        if (cancelled) return;
        setBatches(b.batches || []);
        setRuns(r.runs || []);
        setDatasets(avail.datasets || []); // 修复：之前从未加载，导致筛选区为空、示例命令无法拼真实路径
        setError(null);
      })
      .catch((e) => !cancelled && setError(e.message))
      .finally(() => !cancelled && setLoading(false));
    return () => {
      cancelled = true;
    };
  }, []);

  // 轻量轮询：仅刷新每个批次的实时进度（不打断展开/选择状态）
  useEffect(() => {
    let cancelled = false;
    const tick = () => {
      api.batches()
        .then((r) => {
          if (cancelled) return;
          const prog = new Map((r.batches || []).map((b) => [b.id, b.progress]));
          setBatches((prev) => (prev || []).map((b) => {
            const p = prog.get(b.id);
            return p ? { ...b, progress: p } : b;
          }));
        })
        .catch(() => {});
    };
    const t = setInterval(tick, 4000);
    return () => {
      cancelled = true;
      clearInterval(t);
    };
  }, []);

  // 加载数据集指标（行数/类标签/属性等），不阻塞主列表渲染
  useEffect(() => {
    let cancelled = false;
    api.datasetAnalysis()
      .then((r) => {
        if (cancelled) return;
        const m = {};
        (r.datasets || []).forEach((d) => { m[d.name] = d; });
        setMetaMap(m);
      })
      .catch(() => {});
    return () => {
      cancelled = true;
    };
  }, []);

  // Deep-link support: open the batch named/id'd in the URL hash (#/batches/<id|name>).
  useEffect(() => {
    if (focusBatch == null || !batches.length) return;
    const id = batches.find((b) => String(b.id) === focusBatch || b.name === focusBatch)?.id;
    if (id != null) setOpenId(id);
  }, [focusBatch, batches]);

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

  // 数据集按首字母分组（通讯录式），便于在大量数据集里快速定位与整组勾选
  const groups = useMemo(() => {
    const q = dsQuery.trim().toLowerCase();
    const buckets = new Map();
    datasets.forEach((d, i) => {
      if (q && !d.name.toLowerCase().includes(q)) return;
      const letter = (d.name[0] || '#').toUpperCase();
      if (!buckets.has(letter)) buckets.set(letter, []);
      buckets.get(letter).push({ d, i });
    });
    return [...buckets.entries()]
      .sort(([a], [b]) => a.localeCompare(b))
      .map(([letter, items]) => ({ letter, items }));
  }, [datasets, dsQuery]);

  // 按指标排序时的扁平列表（字母模式仍用分组通讯录）
  const sortedFlat = useMemo(() => {
    if (dsSort === 'alpha') return null;
    const q = dsQuery.trim().toLowerCase();
    return datasets
      .map((d, i) => ({ d, i, m: metaMap[d.name] }))
      .filter((x) => x.m && (!q || x.d.name.toLowerCase().includes(q)))
      .sort((a, b) => (b.m[dsSort] || 0) - (a.m[dsSort] || 0));
  }, [datasets, metaMap, dsSort, dsQuery]);

  function toggleIn(setFn, key) {
    setFn((prev) => {
      const next = new Set(prev);
      if (next.has(key)) next.delete(key);
      else next.add(key);
      return next;
    });
  }

  function selectAllVisible() {
    setSelDs((prev) => {
      const next = new Set(prev);
      groups.forEach((g) => g.items.forEach(({ i }) => next.add(i)));
      return next;
    });
  }
  function clearAll() {
    setSelDs(new Set());
  }
  function toggleGroup(letter) {
    setSelDs((prev) => {
      const next = new Set(prev);
      const g = groups.find((x) => x.letter === letter);
      if (!g) return prev;
      const allOn = g.items.every(({ i }) => next.has(i));
      g.items.forEach(({ i }) => (allOn ? next.delete(i) : next.add(i)));
      return next;
    });
  }
  function groupAllOn(g) {
    return g.items.length > 0 && g.items.every(({ i }) => selDs.has(i));
  }

  // 从 localStorage 恢复上一次成功提交的选择（数据集按 data 路径匹配，算法取交集）
  function restoreLast() {
    try {
      const snap = JSON.parse(localStorage.getItem('petal-lab:lastBatch') || 'null');
      if (!snap) return;
      const byData = new Map(datasets.map((d, i) => [d.data, i]));
      const sel = new Set();
      (snap.datasets || []).forEach((d) => {
        const i = byData.get(d.data);
        if (i != null) sel.add(i);
      });
      setSelDs(sel);
      setSelLr(new Set((snap.learners || []).filter((l) => LEARNERS.includes(l))));
      setBatchName(snap.batchName || '');
      setFolds(snap.folds || 10);
      setDiscretiser(snap.discretiser || 'mdl');
    } catch {}
  }

  async function runBatch() {
    const ds = [...selDs].map((i) => datasets[i]).filter(Boolean);
    const lr = [...selLr];
    if (!ds.length || !lr.length) {
      setFormErr('请至少选择一个数据集和一个算法');
      return;
    }
    setFormErr(null);
    setSubmitting(true);
    setFormResult(null);
    try {
      const plan = {
        name: batchName.trim() || `batch-${ds.length}x${lr.length}`,
        datasets: ds.map((d) => ({ meta: d.meta, data: d.data })),
        learners: lr,
        mode: 'xval',
        folds: Number(folds) || 10,
        discretiser,
      };
      const r = await api.batchRun(plan);
      setFormResult(r);
      try {
        const snap = {
          datasets: ds.map((d) => ({ meta: d.meta, data: d.data })),
          learners: lr,
          batchName: batchName.trim(),
          folds: Number(folds) || 10,
          discretiser,
        };
        localStorage.setItem('petal-lab:lastBatch', JSON.stringify(snap));
        setHasLast(true);
      } catch {}
      // 提交成功后刷新列表并直接进入该批次，无需手动刷新页面
      try {
        const [b, rr] = await Promise.all([api.batches(), api.runs({ limit: 1000 })]);
        setBatches(b.batches || []);
        setRuns(rr.runs || []);
      } catch {}
      setOpenId(r.batch_id);
      onOpenBatch?.(String(r.batch_id));
    } catch (e) {
      setFormErr(e.message);
    } finally {
      setSubmitting(false);
    }
  }

  if (openId != null) {
    const batch = batches.find((b) => b.id === openId);
    return (
      <BatchAnalysis batch={batch} onBack={() => { setOpenId(null); onOpenBatch?.(null); }} />
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

      <div className="card" style={{ marginBottom: 18 }}>
        <h2>新建批实验 <span className="tag">勾选</span></h2>
        <p className="sub">
          勾选多个数据集和算法，一次性跑完整个「数据集 × 算法」网格，归入同一批次后做横向对比。
          这跟网页上提交单次运行一样，底层都是直接调用 <code className="mono">petal</code> 二进制并自动入库。
        </p>

        <div className="field">
          <label>数据集（可多选，已选 {selDs.size} 个，共 {datasets.length} 个）</label>
          <input
            className="ds-search"
            placeholder="搜索数据集…"
            value={dsQuery}
            onChange={(e) => setDsQuery(e.target.value)}
            style={{ marginBottom: 8 }}
          />
          {loading ? (
            <div className="muted" style={{ padding: '8px 2px' }}>加载中…</div>
          ) : datasets.length === 0 ? (
            <div className="muted" style={{ padding: '8px 2px' }}>未找到可用数据集（检查 data/ 目录）</div>
          ) : (
            <>
              <div className="ds-toolbar">
                <button type="button" className="ds-mini-btn" onClick={selectAllVisible}>全选</button>
                <button type="button" className="ds-mini-btn" onClick={clearAll}>清空</button>
                <span className="muted" style={{ fontSize: 12 }}>按首字母分组，点分组「全选」可整组勾选</span>
                <button
                  type="button"
                  className="ds-mini-btn"
                  style={{ marginLeft: 'auto' }}
                  onClick={restoreLast}
                  disabled={!hasLast}
                  title={hasLast ? '恢复上一次提交的选择' : '暂无上一次选择'}
                >
                  恢复上次选择
                </button>
              </div>
              <div className="ds-sort">
                <span className="muted" style={{ fontSize: 12, marginRight: 6 }}>排序</span>
                {DS_SORTS.map((s) => (
                  <button
                    type="button"
                    key={s.id}
                    className={`ds-mini-btn ${dsSort === s.id ? 'on' : ''}`}
                    disabled={s.id !== 'alpha' && !metaReady}
                    title={s.id !== 'alpha' && !metaReady ? '指标加载中…' : ''}
                    onClick={() => setDsSort(s.id)}
                  >
                    {s.label}
                  </button>
                ))}
              </div>
              {dsSort === 'alpha' ? (
                <div className="ds-groups">
                  {groups.map((g) => (
                    <div className="ds-group" key={g.letter}>
                      <div className="ds-group-head">
                        <span className="ds-letter">{g.letter}</span>
                        <button
                          type="button"
                          className="ds-mini-btn"
                          onClick={() => toggleGroup(g.letter)}
                        >
                          {groupAllOn(g) ? '取消' : '全选'}
                        </button>
                      </div>
                      <div className="ds-chips">
                        {g.items.map(({ d, i }) => (
                          <button
                            type="button"
                            key={d.data}
                            title={d.name}
                            className={`ds-chip ${selDs.has(i) ? 'on' : ''}`}
                            onClick={() => toggleIn(setSelDs, i)}
                          >
                            {d.name}
                          </button>
                        ))}
                      </div>
                    </div>
                  ))}
                </div>
              ) : (
                <div className="ds-chips ds-flat">
                  {sortedFlat.length === 0 ? (
                    <div className="muted" style={{ padding: '8px 2px' }}>无匹配数据集</div>
                  ) : (
                    sortedFlat.map(({ d, i, m }) => (
                      <button
                        type="button"
                        key={d.data}
                        title={`${d.name} · ${m.n_records?.toLocaleString()} 行 · ${m.n_classes} 类标签`}
                        className={`ds-chip ${selDs.has(i) ? 'on' : ''}`}
                        onClick={() => toggleIn(setSelDs, i)}
                      >
                        {d.name}
                        <span className="ds-metric-badge">{m[dsSort]?.toLocaleString()}</span>
                      </button>
                    ))
                  )}
                </div>
              )}
            </>
          )}
        </div>

        <div className="field">
          <label>算法（可多选，已选 {selLr.size} 个）</label>
          <div className="chips">
            {LEARNERS.map((l) => (
              <button
                type="button"
                key={l}
                className={`algo-chip ${selLr.has(l) ? 'on' : ''}`}
                onClick={() => toggleIn(setSelLr, l)}
              >
                {l}
              </button>
            ))}
          </div>
        </div>

        <div className="grid2" style={{ gridTemplateColumns: '1fr 1fr', gap: 14 }}>
          <div className="field">
            <label>批次名</label>
            <input placeholder="例如 grid1" value={batchName} onChange={(e) => setBatchName(e.target.value)} />
          </div>
          <div className="field">
            <label>折数</label>
            <input type="number" min="2" max="100" value={folds} onChange={(e) => setFolds(e.target.value)} />
          </div>
        </div>

        <div className="field">
          <label>离散化（含数值属性时建议开启）</label>
          <select value={discretiser} onChange={(e) => setDiscretiser(e.target.value)}>
            {DISCRETISERS.map((d) => (
              <option key={d.id} value={d.id}>{d.label}</option>
            ))}
          </select>
        </div>

        {formErr && <div className="error" style={{ marginTop: 4 }}>{formErr}</div>}
        {formResult && (
          <div className="notice" style={{ marginTop: 10, marginBottom: 0 }}>
            已提交 {formResult.jobs?.length ?? 0} 个任务到批次 #{formResult.batch_id}。
          </div>
        )}

        <div className="toolbar" style={{ marginTop: 12, marginBottom: 0 }}>
          <button className="btn primary" onClick={runBatch} disabled={submitting}>
            {submitting ? '运行中…' : '运行批次'}
          </button>
        </div>
      </div>

      {error && <div className="error">{error}</div>}
      {loading && <div className="empty">加载中…</div>}

      {!loading && batches.length === 0 && (
        <div className="empty">
          还没有批实验。
          <br />
          <br />
          <span className="mono">
            {datasets.length
              ? `petal ${datasets[0].meta} ${datasets[0].data} -dmdl -v2 -x10 -lnb   # 每个数据集×算法各跑一次`
              : 'petal <数据集.pmeta> <数据集.pdata> -dmdl -v2 -x10 -lnb   # 每个数据集×算法各跑一次'}
          </span>
          <br />
          <br />
          或者在此页勾选数据集与算法后点「运行批次」：
          <br />
          <span className="mono">
            {datasets.length
              ? `petal ${datasets[0].meta} ${datasets[0].data} -dmdl -v2 -x10 -lnb`
              : 'petal <数据集.pmeta> <数据集.pdata> -dmdl -v2 -x10 -lnb'}
          </span>
        </div>
      )}

      {batches.length > 0 && (
        <div className="card">
          <h2>全部批次</h2>
          <p className="sub">共 {batches.length} 个批次。点击任意一行进入批次分析。</p>
          <div className="runlist batches">
            <div className="run-head">
              <div className="c-name"><span className="h-text">批次</span></div>
              <div className="c-num"><span className="h-text">运行数</span></div>
              <div className="c-ds"><span className="h-text">数据集</span></div>
              <div className="c-algo"><span className="h-text">算法</span></div>
              <div className="c-created"><span className="h-text">创建时间</span></div>
              <div className="c-act"><span className="h-text">操作</span></div>
            </div>
            {batches.map((b) => {
              const s = stats.get(b.id);
              const nDatasets = s ? s.datasets.size : 0;
              const nLearners = s ? s.learners.size : 0;
              return (
                <div
                  key={b.id}
                  className={`run-row ${openId === b.id ? 'active' : ''}`}
                  onClick={() => { setOpenId(b.id); onOpenBatch?.(String(b.id)); }}
                >
                  <div className="c-name">
                    <div className="batch-name">{b.name}</div>
                    {b.description && (
                      <div className="muted" style={{ fontSize: 11.5 }}>{b.description}</div>
                    )}
                    {b.progress && (b.progress.running || 0) + (b.progress.pending || 0) > 0 && (
                      <div className="bp-mini" title={`已完成 ${b.progress.done}/${b.progress.total}${b.progress.failed ? ` · 失败 ${b.progress.failed}` : ''}`}>
                        <span className="bp-mini-track"><span style={{ width: `${b.progress.percent}%` }} /></span>
                        <span className="muted" style={{ fontSize: 11 }}>{b.progress.done}/{b.progress.total}{b.progress.failed ? ` · 失败${b.progress.failed}` : ''}</span>
                      </div>
                    )}
                  </div>
                  <div className="c-num">{b.run_count}</div>
                  <div className="c-ds">
                    <span className="pill accent">{nDatasets} 个</span>
                  </div>
                  <div className="c-algo">
                    <span className="pill slate">{nLearners} 个</span>
                  </div>
                  <div className="c-created">
                    {b.created_at.slice(0, 19).replace('T', ' ')}
                  </div>
                  <div className="c-act">
                    <button
                      type="button"
                      className="link-btn"
                      onClick={(e) => {
                        e.stopPropagation();
                        setOpenId(b.id);
                        onOpenBatch?.(String(b.id));
                      }}
                    >
                      分析 →
                    </button>
                  </div>
                </div>
              );
            })}
          </div>
          <div className="notice" style={{ marginTop: 14, marginBottom: 0 }}>
            跨数据集统计需要<strong>至少 2 个数据集</strong>且每个算法在每个数据集上都有结果；
            Friedman 检验要有统计效力通常还要更多（经验上 10 个以上）。
          </div>
        </div>
      )}

    </>
  );
}
