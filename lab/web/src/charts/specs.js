// Vega-Lite specifications.
//
// Kept as pure data so a spec can be reviewed, reused and exported on its own —
// which matters when a figure ends up in a paper and someone asks how it was
// produced.
//
// 配色取自 ui.pen 设计稿（森林绿系）：网格/坐标轴用绿系灰，折线/ROC/排名用
// 绿色主导的离散 scheme，混淆矩阵与数据集×算法热力图用 5 级绿梯度（heat）。

// Light horizontal gridlines make values readable on screen without the chart
// looking graph-paper-busy.
const GRID = { grid: true, gridColor: '#E3E8DF', gridDash: [2, 3], domainColor: '#D6DDD0' };
const AXIS = { labelFontSize: 11.5, titleFontSize: 12.5, labelColor: '#4A6B52', titleColor: '#1B3A28' };
const LEGEND = { labelFontSize: 11.5, titleFontSize: 12, labelColor: '#4A6B52', titleColor: '#1B3A28' };
// 绿色主导的离散配色，保证多学习器可区分且与主题协调
const SCHEME = [
  '#2D5E3A', '#3D6B78', '#A67C2E', '#A6453A', '#6B8F5E',
  '#7A5C2E', '#4A7C55', '#8A6D3B', '#5C8A5E', '#9C6B3F',
];
// 5 级绿梯度（与设计稿 heat-1 ~ heat-5 一致）
const HEAT = ['#E6EFDF', '#C8DBBC', '#93B48C', '#5C8A5E', '#2D5E3A'];

/**
 * Learning curve with a shaded min–max band across trials.
 * Expects rows of { learner, trainSize, mean, lower, upper }.
 */
export function learningCurveSpec({ logX = true } = {}) {
  return {
    $schema: 'https://vega.github.io/schema/vega-lite/v5.json',
    height: 330,
    layer: [
      {
        mark: { type: 'area', opacity: 0.15 },
        encoding: {
          y: { field: 'lower', type: 'quantitative' },
          y2: { field: 'upper', type: 'quantitative' },
        },
      },
      {
        mark: { type: 'line', point: { filled: true, size: 45 }, strokeWidth: 2.4 },
        encoding: {
          y: {
            field: 'mean',
            type: 'quantitative',
            title: '0-1 损失',
            axis: { ...AXIS, ...GRID },
            scale: { zero: false, nice: true },
          },
          tooltip: [
            { field: 'learner', title: '学习器' },
            { field: 'trainSize', title: '训练样本量' },
            { field: 'mean', title: '均值', format: '.4f' },
            { field: 'lower', title: '最小', format: '.4f' },
            { field: 'upper', title: '最大', format: '.4f' },
          ],
        },
      },
    ],
    encoding: {
      x: {
        field: 'trainSize',
        type: 'quantitative',
        title: '训练样本量',
        scale: logX ? { type: 'log' } : undefined,
        axis: { ...AXIS, grid: false },
      },
      color: {
        field: 'learner',
        type: 'nominal',
        title: '学习器',
        scale: { scheme: SCHEME },
        legend: LEGEND,
      },
    },
  };
}

/** Per-fold spread. Expects rows of { learner, value }. */
export function boxPlotSpec() {
  return {
    $schema: 'https://vega.github.io/schema/vega-lite/v5.json',
    height: 320,
    mark: { type: 'boxplot', extent: 'min-max', size: 40 },
    encoding: {
      x: {
        field: 'learner',
        type: 'nominal',
        title: '学习器',
        axis: { ...AXIS, labelAngle: 0, grid: false },
      },
      y: {
        field: 'value',
        type: 'quantitative',
        title: '0-1 损失',
        axis: { ...AXIS, ...GRID },
        scale: { zero: false, nice: true },
      },
      color: {
        field: 'learner',
        type: 'nominal',
        title: '学习器',
        scale: { scheme: SCHEME },
        legend: LEGEND,
      },
    },
  };
}

/** ROC curve. Expects rows of { learner, fpr, tpr }. */
export function rocSpec() {
  return {
    $schema: 'https://vega.github.io/schema/vega-lite/v5.json',
    height: 330,
    layer: [
      {
        data: { values: [{ x: 0, y: 0 }, { x: 1, y: 1 }] },
        mark: { type: 'line', strokeDash: [5, 4], color: '#7A9A80', strokeWidth: 1.4 },
        encoding: {
          x: { field: 'x', type: 'quantitative' },
          y: { field: 'y', type: 'quantitative' },
        },
      },
      {
        mark: { type: 'line', strokeWidth: 2.4, point: false },
        encoding: {
          x: {
            field: 'fpr', type: 'quantitative', title: 'False Positive Rate',
            axis: { ...AXIS, ...GRID }, scale: { domain: [0, 1] },
          },
          y: {
            field: 'tpr', type: 'quantitative', title: 'True Positive Rate',
            axis: { ...AXIS, ...GRID }, scale: { domain: [0, 1] },
          },
          color: {
            field: 'learner', type: 'nominal', title: '学习器',
            scale: { scheme: SCHEME }, legend: LEGEND,
          },
          tooltip: [
            { field: 'learner', title: '学习器' },
            { field: 'fpr', title: 'FPR', format: '.4f' },
            { field: 'tpr', title: 'TPR', format: '.4f' },
          ],
        },
      },
    ],
  };
}

/** Precision–Recall curve. Expects rows of { learner, recall, precision }. */
export function prSpec() {
  return {
    $schema: 'https://vega.github.io/schema/vega-lite/v5.json',
    height: 330,
    mark: { type: 'line', strokeWidth: 2.4 },
    encoding: {
      x: {
        field: 'recall', type: 'quantitative', title: 'Recall',
        axis: { ...AXIS, ...GRID }, scale: { domain: [0, 1] },
      },
      y: {
        field: 'precision', type: 'quantitative', title: 'Precision',
        axis: { ...AXIS, ...GRID }, scale: { domain: [0, 1] },
      },
      color: {
        field: 'learner', type: 'nominal', title: '学习器',
        scale: { scheme: SCHEME }, legend: LEGEND,
      },
      tooltip: [
        { field: 'learner', title: '学习器' },
        { field: 'recall', title: 'Recall', format: '.4f' },
        { field: 'precision', title: 'Precision', format: '.4f' },
      ],
    },
  };
}

/** Confusion matrix. Expects rows of { t, p, count }. */
export function confusionSpec() {
  return {
    $schema: 'https://vega.github.io/schema/vega-lite/v5.json',
    height: 330,
    mark: { type: 'rect', stroke: '#fff', strokeWidth: 1.5 },
    encoding: {
      x: { field: 'p', type: 'ordinal', title: '预测类别', axis: { ...AXIS, grid: false } },
      y: { field: 't', type: 'ordinal', title: '真实类别', axis: { ...AXIS, grid: false } },
      color: {
        field: 'count', type: 'quantitative', title: '样本数',
        scale: { range: HEAT }, legend: LEGEND,
      },
      tooltip: [
        { field: 't', title: '真实' },
        { field: 'p', title: '预测' },
        { field: 'count', title: '样本数' },
      ],
    },
  };
}

/** Average ranks across data sets. Expects rows of { learner, rank }. */
export function rankSpec() {
  return {
    $schema: 'https://vega.github.io/schema/vega-lite/v5.json',
    mark: { type: 'bar', height: { band: 0.55 }, cornerRadiusEnd: 3 },
    encoding: {
      x: {
        field: 'rank', type: 'quantitative', title: '平均排名（越小越好）',
        axis: { ...AXIS, ...GRID },
      },
      y: {
        field: 'learner', type: 'nominal', sort: { field: 'rank', op: 'mean' },
        title: null, axis: { ...AXIS, grid: false },
      },
      color: {
        field: 'learner', type: 'nominal', title: '学习器',
        scale: { scheme: SCHEME }, legend: LEGEND,
      },
      tooltip: [
        { field: 'learner', title: '学习器' },
        { field: 'rank', title: '平均排名', format: '.4f' },
      ],
    },
  };
}

/**
 * Critical difference diagram (Demšar 2006).
 *
 * Algorithms are placed at their average rank. Bars join algorithms whose rank
 * difference is below the critical difference — i.e. those that are *not*
 * significantly different.
 *
 * Points and bars occupy separate rows so the bars do not run through the
 * points, which made the earlier single-row version hard to read.
 *
 * `points` rows: { learner, rank, y }
 * `bars` rows:   { x, x2, y }
 */
export function cdDiagramSpec({ k = 3, points = [], bars = [] } = {}) {
  // Named datasets must be declared here; passing them via the component's
  // `data` prop leaves them unresolved and Vega fails to render.
  return {
    $schema: 'https://vega.github.io/schema/vega-lite/v5.json',
    height: 190,
    datasets: { points, bars },
    layer: [
      {
        data: { name: 'bars' },
        mark: { type: 'rule', strokeWidth: 5, strokeCap: 'round', color: '#1B3A28' },
        encoding: {
          x: { field: 'x', type: 'quantitative' },
          x2: { field: 'x2', type: 'quantitative' },
          y: { field: 'y', type: 'nominal' },
        },
      },
      {
        data: { name: 'points' },
        mark: { type: 'point', filled: true, size: 190 },
        encoding: {
          x: { field: 'rank', type: 'quantitative' },
          y: { field: 'y', type: 'nominal' },
          color: {
            field: 'learner', type: 'nominal', title: '学习器',
            scale: { scheme: SCHEME }, legend: { orient: 'bottom', ...LEGEND },
          },
          tooltip: [
            { field: 'learner', title: '学习器' },
            { field: 'rank', title: '平均排名', format: '.4f' },
          ],
        },
      },
    ],
    encoding: {
      x: {
        type: 'quantitative',
        title: '平均排名（越小越好）',
        axis: { ...AXIS, grid: false, orient: 'top' },
        scale: { zero: false, padding: 2, nice: false },
      },
      y: {
        type: 'nominal',
        title: null,
        axis: { ...AXIS, grid: false, labelFontSize: 11.5, labelPadding: 6 },
      },
    },
  };
}

/** Data set × algorithm heat map. Expects rows of { dataset, learner, value }. */
export function matrixHeatmapSpec() {
  return {
    $schema: 'https://vega.github.io/schema/vega-lite/v5.json',
    height: 320,
    mark: { type: 'rect', stroke: '#fff', strokeWidth: 1.5 },
    encoding: {
      x: {
        field: 'learner', type: 'nominal', title: '学习器',
        axis: { ...AXIS, labelAngle: 0, grid: false },
      },
      y: {
        field: 'dataset', type: 'nominal', title: '数据集',
        axis: { ...AXIS, grid: false },
      },
      color: {
        field: 'value', type: 'quantitative', title: '0-1 损失',
        scale: { range: HEAT }, legend: LEGEND,
      },
      tooltip: [
        { field: 'dataset', title: '数据集' },
        { field: 'learner', title: '学习器' },
        { field: 'value', title: '指标', format: '.4f' },
      ],
    },
  };
}
