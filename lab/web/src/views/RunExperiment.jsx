import { useEffect, useMemo, useRef, useState } from 'react';
import { api } from '../api.js';
import StatusBadge from '../components/StatusBadge.jsx';

const LEARNERS = ['nb', 'aode', 'tan', 'kdb', 'cbn', 'gnb', 'a2de', 'a3de', 'DTree', 'AdaBoost'];
const DISCRETISERS = [
  { id: '', label: '不离散化' },
  { id: 'mdl', label: 'MDL 离散化' },
  { id: 'mdl2', label: 'MDL 二值离散化' },
  { id: 'equal-frequency', label: '等频离散化' },
  { id: 'equal-depth', label: '等深离散化' },
];

function basename(p) {
  return String(p || '').split(/[\\/]/).pop() || p;
}

export default function RunExperiment({ onViewRun }) {
  const [datasets, setDatasets] = useState([]);
  const [form, setForm] = useState({
    dataset: '', mode: 'xval', folds: 10, learners: ['nb'],
    discretiser: 'mdl', batch: '', holdout: 1000, trials: 5,
  });
  const [job, setJob] = useState(null);
  const [log, setLog] = useState([]);
  const [error, setError] = useState(null);
  const [submitting, setSubmitting] = useState(false);
  const logRef = useRef(null);
  const streamRef = useRef(null);

  useEffect(() => {
    api.availableDatasets()
      .then((d) => {
        const list = d.datasets || [];
        setDatasets(list);
        if (list.length && !form.dataset) {
          setForm((f) => ({ ...f, dataset: list[0].data }));
        }
      })
      .catch((e) => setError(e.message));
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, []);

  useEffect(() => () => streamRef.current?.close(), []);

  useEffect(() => {
    if (logRef.current) logRef.current.scrollTop = logRef.current.scrollHeight;
  }, [log]);

  const selected = useMemo(
    () => datasets.find((d) => d.data === form.dataset),
    [datasets, form.dataset]
  );

  const multiLearner = form.mode === 'learningCurves';

  function toggleLearner(name) {
    setForm((f) => {
      const has = f.learners.includes(name);
      if (!multiLearner) return { ...f, learners: [name] };
      return {
        ...f,
        learners: has ? f.learners.filter((x) => x !== name) : [...f.learners, name],
      };
    });
  }

  async function submit(e) {
    e?.preventDefault();
    setError(null);
    setSubmitting(true);
    setLog([]);
    try {
      if (!selected) throw new Error('请先选择数据集');
      const spec = {
        meta: selected.meta,
        data: selected.data,
        mode: form.mode,
        folds: Number(form.folds),
        learners: form.learners,
        discretiser: form.discretiser,
        batch: form.batch || undefined,
      };
      if (form.mode === 'learningCurves') {
        spec.holdout = Number(form.holdout);
        spec.trials = Number(form.trials);
      }
      const created = await api.submitJob(spec);
      setJob(created);
      streamRef.current = api.streamJob(created.id, {
        onLog: (line) => setLog((ls) => [...ls, line]),
        onStatus: (snap) => setJob((j) => ({ ...(j || {}), ...snap })),
        onDone: (snap) => {
          setJob(snap);
          setSubmitting(false);
        },
      });
    } catch (err) {
      setError(err.message);
      setSubmitting(false);
    }
  }

  async function cancel() {
    if (!job) return;
    await api.cancelJob(job.id);
    setJob((j) => ({ ...j, status: 'cancelled' }));
    setSubmitting(false);
  }

  const pct = useMemo(() => {
    const p = job?.progress;
    if (!p || !p.total) return null;
    return Math.min(100, Math.round((p.current / p.total) * 100));
  }, [job]);

  return (
    <>
      <div className="page-head">
        <h1>运行实验</h1>
        <p>填写参数后直接在页面上启动 petal，实时查看输出，完成后自动存入结果库。</p>
      </div>

      <div className="grid2" style={{ gridTemplateColumns: 'minmax(380px, 36%) 1fr' }}>
        <form className="card form-pill" onSubmit={submit} style={{ marginBottom: 0 }}>
          <h2>运行参数</h2>
          <p className="sub">配置完成后点击运行，petal 会在本地执行。</p>

          <div className="field">
            <label>数据集</label>
            <select
              value={form.dataset}
              onChange={(e) => setForm((f) => ({ ...f, dataset: e.target.value }))}
            >
              {datasets.length === 0 && <option value="">（加载中…）</option>}
              {datasets.map((d) => (
                <option key={d.data} value={d.data}>
                  {d.name}  ({(d.size / 1024).toFixed(0)} KB)
                </option>
              ))}
            </select>
          </div>

          <div className="field">
            <label>实验模式</label>
            <select
              value={form.mode}
              onChange={(e) => setForm((f) => ({
                ...f,
                mode: e.target.value,
                learners: e.target.value !== 'learningCurves' ? [f.learners[0]] : f.learners,
              }))}
            >
              <option value="xval">交叉验证</option>
              <option value="learningCurves">学习曲线</option>
            </select>
          </div>

          {form.mode === 'xval' && (
            <div className="field">
              <label>折数</label>
              <input
                type="number" min="2" max="100" value={form.folds}
                onChange={(e) => setForm((f) => ({ ...f, folds: e.target.value }))}
              />
            </div>
          )}

          {form.mode === 'learningCurves' && (
            <>
              <div className="field">
                <label>测试集大小</label>
                <input
                  type="number" min="10" value={form.holdout}
                  onChange={(e) => setForm((f) => ({ ...f, holdout: e.target.value }))}
                />
              </div>
              <div className="field">
                <label>试验次数</label>
                <input
                  type="number" min="1" value={form.trials}
                  onChange={(e) => setForm((f) => ({ ...f, trials: e.target.value }))}
                />
              </div>
            </>
          )}

          <div className="field">
            <label>离散化（含数值属性时必需）</label>
            <select
              value={form.discretiser}
              onChange={(e) => setForm((f) => ({ ...f, discretiser: e.target.value }))}
            >
              {DISCRETISERS.map((d) => (
                <option key={d.id} value={d.id}>{d.label}</option>
              ))}
            </select>
          </div>

          <div className="field">
            <label>算法{multiLearner ? '（可多选）' : ''}</label>
            <div className="chips">
              {LEARNERS.map((l) => {
                const on = form.learners.includes(l);
                return (
                  <button
                    type="button"
                    key={l}
                    className={`algo-chip ${on ? 'on' : ''}`}
                    onClick={() => toggleLearner(l)}
                  >
                    {l}
                  </button>
                );
              })}
            </div>
          </div>

          <div className="field">
            <label>归入批次（可选）</label>
            <input
              placeholder="例如 exp1"
              value={form.batch}
              onChange={(e) => setForm((f) => ({ ...f, batch: e.target.value }))}
            />
          </div>

          {error && <div className="error">{error}</div>}

          <div className="toolbar" style={{ marginTop: 14, marginBottom: 0 }}>
            <button className="btn primary" type="submit" disabled={submitting}>
              {submitting ? '运行中…' : '运行'}
            </button>
            {submitting && (
              <button className="btn danger-outline" type="button" onClick={cancel}>取消</button>
            )}
          </div>
        </form>

        <div>
          <div className="card">
            <h2>
              输出
              {job && <StatusBadge state={job.status} />}
            </h2>
            <p className="sub">
              {job ? <span className="mono">{job.command}</span> : '提交后，petal 的输出会实时显示在这里。'}
            </p>

            {pct !== null && (
              <div className="progress">
                <div className="meta">
                  <span>{job.progress.label || `${job.progress.current}/${job.progress.total} 个任务`}</span>
                  <span className="pct">{pct}%</span>
                </div>
                <div className="track">
                  <div className="bar" style={{ width: `${pct}%` }} />
                  <span className="label">{pct}%</span>
                </div>
              </div>
            )}

            <div className="terminal" ref={logRef}>
              {log.length === 0 && !job && (
                <div className="muted">尚未开始。</div>
              )}
              {log.map((line, i) => (
                <div key={i} className="line">{line}</div>
              ))}
            </div>

            {job?.status === 'done' && job.run_id && (
              <div className="notice" style={{ marginTop: 12, marginBottom: 0 }}>
                已完成并存入结果库（run #{job.run_id}）。
                <button
                  className="btn"
                  style={{ marginLeft: 10 }}
                  onClick={() => onViewRun?.(job.run_id)}
                >
                  查看结果 →
                </button>
              </div>
            )}
            {job?.status === 'failed' && (
              <div className="error" style={{ marginTop: 12, marginBottom: 0 }}>
                运行失败：{job.error || `退出码 ${job.exit_code}`}
              </div>
            )}
          </div>
        </div>
      </div>
    </>
  );
}
