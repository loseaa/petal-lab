// 统一的线性图标集（Lucide 风格，stroke = currentColor）。
// 集中管理可保证导航与各处图标风格一致：相同描边宽度、相同视觉重量、
// 相同的 24×24 视图框，跟随主题色变化。
const PATHS = {
  // 总览：仪表盘网格
  overview: (
    <>
      <rect x="3" y="3" width="7" height="9" rx="1.5" />
      <rect x="14" y="3" width="7" height="5" rx="1.5" />
      <rect x="14" y="12" width="7" height="9" rx="1.5" />
      <rect x="3" y="16" width="7" height="5" rx="1.5" />
    </>
  ),
  // 运行实验：播放三角
  run: <polygon points="7 4.5 19 12 7 19.5 7 4.5" />,
  // 批实验：堆叠图层
  batches: (
    <>
      <polygon points="12 2 3 7 12 12 21 7 12 2" />
      <polyline points="3 12 12 17 21 12" />
      <polyline points="3 17 12 22 21 17" />
    </>
  ),
  // 单次运行：列表
  runs: (
    <>
      <line x1="8" y1="6" x2="21" y2="6" />
      <line x1="8" y1="12" x2="21" y2="12" />
      <line x1="8" y1="18" x2="21" y2="18" />
      <circle cx="3.5" cy="6" r="1.4" />
      <circle cx="3.5" cy="12" r="1.4" />
      <circle cx="3.5" cy="18" r="1.4" />
    </>
  ),
  // 空态/收件箱插画
  inbox: (
    <>
      <polyline points="22 12 16 12 14 15 10 15 8 12 2 12" />
      <path d="M5.45 5.11 2 12v6a2 2 0 0 0 2 2h16a2 2 0 0 0 2-2v-6l-3.45-6.89A2 2 0 0 0 16.76 4H7.24a2 2 0 0 0-1.79 1.11z" />
    </>
  ),
  // 导出
  download: (
    <>
      <path d="M21 15v4a2 2 0 0 1-2 2H5a2 2 0 0 1-2-2v-4" />
      <polyline points="7 10 12 15 17 10" />
      <line x1="12" y1="15" x2="12" y2="3" />
    </>
  ),
  // 数据库/实验
  flask: (
    <>
      <path d="M9 3h6" />
      <path d="M10 3v6.5L5.5 18a2 2 0 0 0 1.8 3h9.4a2 2 0 0 0 1.8-3L14 9.5V3" />
      <line x1="7.5" y1="14" x2="16.5" y2="14" />
    </>
  ),
  // 数据分析：柱状图
  data: (
    <>
      <line x1="4" y1="20" x2="20" y2="20" />
      <rect x="5" y="11" width="3" height="6" rx="0.6" />
      <rect x="10.5" y="7" width="3" height="10" rx="0.6" />
      <rect x="16" y="13" width="3" height="4" rx="0.6" />
    </>
  ),
};

export default function Icon({ name, size = 18, className = '', strokeWidth = 1.75 }) {
  return (
    <svg
      width={size}
      height={size}
      viewBox="0 0 24 24"
      fill="none"
      stroke="currentColor"
      strokeWidth={strokeWidth}
      strokeLinecap="round"
      strokeLinejoin="round"
      className={className}
      aria-hidden="true"
    >
      {PATHS[name] || null}
    </svg>
  );
}
