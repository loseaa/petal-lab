import { useEffect, useMemo, useState } from 'react';
import { api } from '../api.js';
import Chart from '../components/Chart.jsx';
import {
  learningCurveSpec,
  boxPlotSpec,
  rocSpec,
  prSpec,
  confusionSpec,
} from '../charts/specs.js';

const POOLED = -1;

function mean(xs) {
  return xs.length ? xs.reduce((a, b) => a + b, 0) / xs.length : 0;
}

/** Collapse curve points into mean/min/max per (learner, sample size). */
function summariseCurves(curves) {
  const groups = new Map();
  for (const c of curves || []) {
    const key = `${c.learner}@@${c.train_size}`;
    if (!groups.has(key)) groups.set(key, { learner: c.learner, trainSize: c.train_size, vals: [] });
    groups.get(key).vals.push(c.error);
  }
  return [...groups.values()].map((g) => ({
    learner: g.learner,
    trainSize: g.trainSize,
    mean: mean(g.vals),
    lower: Math.min(...g.vals),
    upper: Math.max(...g.vals),
  })).sort((a, b) => a.trainSize - b.trainSize);
}

/** ROC / PR computed on the client; only aggregate metrics live on the server. */
function rocPr(instances, posClass) {
  const scored = instances
    .map((i) => ({ score: i.probs?.[posClass] ?? 0, pos: i.trueClass === posClass ? 1 : 0 }))
    .sort((a, b) => b.score - a.score);

  const totalPos = scored.reduce((n, x) => n + x.pos, 0);
  const totalNeg = scored.length - totalPos;
  if (!totalPos || !totalNeg) return null;

  let tp = 0, fp = 0, prev = null, auc = 0;
  const roc = [{ fpr: 0, tpr: 0 }];
  for (const s of scored) {
    // Flush a point when the score changes so tied scores form one step.
    if (prev !== null && s.score !== prev) {
      roc.push({ fpr: fp / totalNeg, tpr: tp / totalPos });
    }
    if (s.pos) tp += 1; else fp += 1;
    prev = s.score;
  }
  roc.push({ fpr: fp / totalNeg, tpr: tp / totalPos });

  for (let i = 1; i < roc.length; i += 1) {
    auc += (roc[i].fpr - roc[i - 1].fpr) * ((roc[i].tpr + roc[i - 1].tpr) / 2);
  }

  const pr = roc
    .filter((p) => !(p.fpr === 0 && p.tpr === 0))
    .map((p) => {
      const tpC = p.tpr * totalPos;
      const fpC = p.fpr * totalNeg;
      return { recall: p.tpr, precision: tpC + fpC > 0 ? tpC / (tpC + fpC) : 0 };
    });

  return { roc, pr, auc };
}

export default function RunDetail({ run }) {
  const [detail, setDetail] = useState(null);
  const [preds, setPreds] = useState(null);
  const [error, setError] = useState(null);

  useEffect(() => {
    let cancelled = false;
    setDetail(null);
    setPreds(null);
    api
      .run(run.id)
      .then((d) => {
        if (cancelled) return;
        setDetail(d);
        if (d.prediction_count > 0) {
          api.predictions(d.id).then((p) => !cancelled && setPreds(p.instances || []));
        }
      })
      .catch((e) => !cancelled && setError(e.message));
    return () => {
      cancelled = true;
    };
  }, [run.id]);

  const pooled = useMemo(() => {
    if (!detail) return [];
    return detail.metrics.filter((m) => m.fold === POOLED);
  }, [detail]);

  const folds = useMemo(() => {
    if (!detail) return [];
    return detail.metrics.filter((m) => m.fold >= 0 && m.metric === '0-1_loss');
  }, [detail]);

  const curveData = useMemo(() => summariseCurves(detail?.curves), [detail]);

  const cmData = useMemo(() => {
    const cm = detail?.confusion?.[0];
    if (!cm) return [];
    const n = cm.no_classes;
    const out = [];
    for (let t = 0; t < n; t += 1) {
      for (let p = 0; p < n; p += 1) {
        out.push({ t: `类 ${t}`, p: `类 ${p}`, count: cm.matrix[t * n + p] || 0 });
      }
    }
    return out;
  }, [detail]);

  // Memoise the exact objects handed to the chart; a fresh object every render
  // makes react-vega rebuild and abort the render (blank / NaN figure).
  const foldData = useMemo(
    () => ({ values: folds.map((f) => ({ learner: f.learner, value: f.value })) }),
    [folds]
  );
  const cmChartData = useMemo(() => ({ values: cmData }), [cmData]);
  const curveChartData = useMemo(() => ({ values: curveData }), [curveData]);
  const curves = useMemo(() => {
    if (!preds || !preds.length) return null;
    // Group by learner so a multi-learner run draws one curve each.
    const byLearner = new Map();
    for (const i of preds) {
      if (!byLearner.has(i.learner)) byLearner.set(i.learner, []);
      byLearner.get(i.learner).push(i);
    }
    const out = { roc: [], pr: [], aucs: {} };
    for (const [learner, list] of byLearner) {
      const r = rocPr(list, 1);
      if (!r) continue;
      out.aucs[learner] = r.auc;
      out.roc.push(...r.roc.map((p) => ({ ...p, learner })));
      out.pr.push(...r.pr.map((p) => ({ ...p, learner })));
    }
    return out;
  }, [preds]);

  // Declared after `curves` — referencing it earlier would hit the temporal
  // dead zone and crash the whole view.
  const rocData = useMemo(() => ({ values: curves?.roc ?? [] }), [curves]);
  const prData = useMemo(() => ({ values: curves?.pr ?? [] }), [curves]);

  if (error) return <div className="card"><div className="error">{error}</div></div>;
  if (!detail) return <div className="card"><div className="empty">加载中…</div></div>;

  return (
    <>
      <div className="card">
        <h2>运行 #{detail.id} · {detail.dataset}</h2>
        <p className="sub">
          模式 {detail.mode}
          {detail.command && (
            <>
              <br />
              <code style={{ fontSize: 11 }}>{detail.command}</code>
            </>
          )}
        </p>

        <div className="chips">
          {pooled.map((m, i) => (
            <div className="chip" key={i}>
              <span className="k">{m.learner} · {m.metric}</span>
              <span className="v">{m.value.toFixed(4)}</span>
            </div>
          ))}
          {curves &&
            Object.entries(curves.aucs).map(([learner, auc]) => (
              <div className="chip" key={learner}>
                <span className="k">{learner} · AUC</span>
                <span className="v">{auc.toFixed(4)}</span>
              </div>
            ))}
        </div>
      </div>

      {curveData.length > 0 && (
        <Chart
          title="学习曲线"
          description="0-1 损失随训练样本量变化；阴影为多次试验的极差。"
          spec={learningCurveSpec()}
          data={curveChartData}
        />
      )}

      {folds.length > 0 && (
        <Chart
          title="折间分布"
          description="每一折的 0-1 损失。箱体为四分位距，须线为极值——比单一均值更能反映稳定性。"
          spec={boxPlotSpec()}
          data={foldData}
        />
      )}

      {curves && curves.roc.length > 0 && (
        <div className="grid2">
          <Chart
            title="ROC 曲线"
            description="对角线为随机猜测基准。"
            spec={rocSpec()}
            data={rocData}
          />
          <Chart
            title="PR 曲线"
            description="类别不平衡时，PR 曲线比 ROC 更能反映真实表现。"
            spec={prSpec()}
            data={prData}
          />
        </div>
      )}

      {cmData.length > 0 && (
        <Chart
          title="混淆矩阵"
          description="行真实、列预测。"
          spec={confusionSpec()}
          data={cmChartData}
        />
      )}

      {!curveData.length && !folds.length && !cmData.length && (
        <div className="card">
          <div className="empty">这次运行没有可绘制的结构化数据。</div>
        </div>
      )}
    </>
  );
}
