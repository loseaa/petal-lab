import React from 'react';

const LABELS = {
  pending: '排队中',
  running: '运行中',
  done: '完成',
  failed: '失败',
  cancelled: '已取消',
};

const CLASS = {
  pending: 'queued',
  running: 'running',
  done: 'done',
  failed: 'error',
  cancelled: 'queued',
};

/** Render a task/job lifecycle state as a colored pill. */
export default function StatusBadge({ state, className = '' }) {
  const s = state || 'pending';
  return (
    <span className={`status-badge ${CLASS[s] || 'queued'} ${className}`}>
      {LABELS[s] || s}
    </span>
  );
}
