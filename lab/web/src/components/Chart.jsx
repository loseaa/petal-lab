import { useMemo, useRef, useState } from 'react';
import { VegaLite } from 'react-vega';

/**
 * Chart card with export.
 *
 * Export formats:
 *  - SVG: vector, scales without loss, converts cleanly to PDF — the safe
 *    choice for a paper.
 *  - PNG: rendered at scale 4, roughly 300 dpi at typical figure widths.
 */
function download(blob, filename) {
  const url = URL.createObjectURL(blob);
  const a = document.createElement('a');
  a.href = url;
  a.download = filename;
  a.click();
  URL.revokeObjectURL(url);
}

export default function Chart({ title, description, spec, data }) {
  const viewRef = useRef(null);
  const [ready, setReady] = useState(false);

  // Specs are built inline at the call site (spec={boxPlotSpec()}), so every
  // render hands react-vega a brand-new object. It treats that as a changed
  // spec and rebuilds the chart, tearing down the previous render midway —
  // which shows up as a blank card or NaN coordinates. Keying off the serialised
  // spec keeps the reference stable whenever the content is unchanged.
  const specJson = JSON.stringify(spec);
  const stableSpec = useMemo(() => JSON.parse(specJson), [specJson]);

  // Inline the data into the spec instead of using react-vega's `data` prop:
  // that prop is interpreted as a *named dataset map*, so passing {values: [...]}
  // leaves Vega with no bound data and it renders nothing (no error either).
  // Specs that carry their own `datasets` (the CD diagram) are left alone.
  const finalSpec = useMemo(() => {
    const withData = stableSpec.datasets || !data ? stableSpec : { ...stableSpec, data };
    // Without an explicit width Vega-Lite falls back to 200px, leaving the
    // chart squeezed into a narrow strip regardless of the card size.
    return {
      ...withData,
      width: 'container',
      autosize: { type: 'fit', contains: 'padding' },
    };
  }, [stableSpec, data]);

  // A chart with no rows makes Vega fail while measuring its container, which
  // would take the whole page down — so short-circuit instead.
  const hasData = useMemo(() => {
    if (spec.datasets) return true;
    if (!data) return false;
    const vals = data.values;
    return Array.isArray(vals) ? vals.length > 0 : Boolean(vals);
  }, [spec, data]);

  const filename = (title || 'figure').replace(/[^\w一-龥-]+/g, '_');

  async function exportSVG() {
    if (!viewRef.current) return;
    const svg = await viewRef.current.toSVG();
    download(new Blob([svg], { type: 'image/svg+xml' }), `${filename}.svg`);
  }

  async function exportPNG() {
    if (!viewRef.current) return;
    const url = await viewRef.current.toCanvas(4);
    const res = await fetch(url);
    download(await res.blob(), `${filename}.png`);
  }

  return (
    <div className="chart-card">
      <div className="head">
        <h2>{title}</h2>
        <div className="actions">
          <button
            className="icon-btn"
            onClick={exportSVG}
            disabled={!ready}
            title="矢量格式，可无损缩放，也能转 PDF，适合放进论文"
          >
            SVG
          </button>
          <button
            className="icon-btn"
            onClick={exportPNG}
            disabled={!ready}
            title="4 倍缩放渲染，约合 300 dpi"
          >
            PNG
          </button>
        </div>
      </div>
      {description && <p className="sub">{description}</p>}

      {hasData ? (
        <VegaLite
          spec={finalSpec}
          renderer="svg"
          actions={false}
          style={{ width: '100%' }}
          onNewView={(view) => {
            viewRef.current = view;
            setReady(true);
          }}
        />
      ) : (
        <div className="empty" style={{ padding: 26 }}>暂无数据</div>
      )}
    </div>
  );
}
