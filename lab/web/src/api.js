// Thin wrapper over the petal-lab HTTP API.
// In dev, Vite proxies /api to http://127.0.0.1:8899 (see vite.config.js);
// in production the same origin serves both.

async function getJSON(path, params = {}) {
  const qs = new URLSearchParams(
    Object.entries(params).filter(([, v]) => v !== undefined && v !== null && v !== '')
  ).toString();
  const url = `${path}${qs ? `?${qs}` : ''}`;
  const res = await fetch(url);
  if (!res.ok) {
    const detail = await res.json().catch(() => ({}));
    throw new Error(detail.error || `请求失败: ${res.status}`);
  }
  return res.json();
}

export const api = {
  runs: (params) => getJSON('/api/runs', params),
  run: (id) => getJSON(`/api/runs/${id}`),
  predictions: (id) => getJSON(`/api/runs/${id}/predictions`),
  batches: () => getJSON('/api/batches'),
  batchProgress: (id) => getJSON(`/api/batches/${id}/progress`),
  datasets: () => getJSON('/api/datasets'),
  compare: (params) => getJSON('/api/compare', params),
  createBatch: async (name, description = '') => {
    const res = await fetch('/api/batches', {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({ name, description }),
    });
    return res.json();
  },
  deleteRun: async (id) => {
    const res = await fetch(`/api/runs/${id}`, { method: 'DELETE' });
    return res.json();
  },

  availableDatasets: () => getJSON('/api/datasets/available'),
  datasetAnalysis: () => getJSON('/api/datasets/analysis'),
  datasetDetail: (name) => getJSON(`/api/datasets/analysis/${encodeURIComponent(name)}`),
  jobs: () => getJSON('/api/jobs'),
  job: (id) => getJSON(`/api/jobs/${id}`),
  submitJob: async (spec) => {
    const res = await fetch('/api/jobs', {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify(spec),
    });
    const data = await res.json();
    if (!res.ok) throw new Error(data.error || `提交失败: ${res.status}`);
    return data;
  },
  batchRun: async (plan) => {
    const res = await fetch('/api/batch/run', {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify(plan),
    });
    const data = await res.json();
    if (!res.ok) throw new Error(data.error || `提交失败: ${res.status}`);
    return data;
  },

  cancelJob: async (id) => {
    await fetch(`/api/jobs/${id}/cancel`, { method: 'POST' });
  },

  /**
   * Subscribe to a running job's output.
   * Returns a close() function. `onLog` receives each new line,
   * `onStatus` the job snapshot, `onDone` fires once at the end.
   */
  streamJob: (id, { onLog, onStatus, onDone }) => {
    const es = new EventSource(`/api/jobs/${id}/stream`);
    es.addEventListener('log', (e) => onLog?.(e.data));
    es.addEventListener('status', (e) => {
      try { onStatus?.(JSON.parse(e.data)); } catch { /* ignore */ }
    });
    es.addEventListener('done', (e) => {
      try { onDone?.(JSON.parse(e.data)); } catch { /* ignore */ }
      es.close();
    });
    es.onerror = () => es.close();
    return { close: () => es.close() };
  },
};
