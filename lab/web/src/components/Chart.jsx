import { useEffect, useMemo, useRef, useState } from 'react';
import embed from 'vega-embed';

/**
 * Chart card with export.
 *
 * Export formats:
 *  - SVG: vector, scales without loss, converts cleanly to PDF — the safe
 *    choice for a paper.
 *  - PNG: rendered at scale 4, roughly 300 dpi at typical figure widths.
 *
 * Rendering uses the imperative vega-embed API (not the declarative
 * <VegaLite>) so we can finalize the view in the effect cleanup. The
 * declarative component tears its container down mid-async-render when the
 * card unmounts quickly (e.g. switching between runs), which made vega-embed
 * measure a null element and throw "getBoundingClientRect of null", taking
 * the whole page down to a blank screen.
 */
function download(blob, filename) {
  const url = URL.createObjectURL(blob);
  const a = document.createElement('a');
  a.href = url;
  a.download = filename;
  a.click();
  URL.revokeObjectURL(url);
}

export default function Chart({ title, description, spec, data, plain }) {
  const containerRef = useRef(null);
  const viewRef = useRef(null);
  const [ready, setReady] = useState(false);

  // Specs are built inline at the call site (spec={boxPlotSpec()}), so every
  // render hands us a brand-new object. Keying off the serialised spec keeps
  // the reference stable whenever the content is unchanged, so vega doesn't
  // rebuild on every render.
  const specJson = JSON.stringify(spec);
  const stableSpec = useMemo(() => JSON.parse(specJson), [specJson]);

  // Inline the data into the spec instead of using react-vega's `data` prop:
  // that prop is interpreted as a *named dataset map*, so passing {values: [...]}
  // leaves Vega with no bound data and it renders nothing. Specs that carry
  // their own `datasets` (the CD diagram) are left alone.
  const finalSpec = useMemo(() => {
    const withData = stableSpec.datasets || !data ? stableSpec : { ...stableSpec, data };
    // Without an explicit width Vega-Lite falls back to 200px, squeezing the
    // chart into a narrow strip regardless of the card size.
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

  useEffect(() => {
    if (!hasData || !containerRef.current) return undefined;
    let cancelled = false;
    setReady(false);
    embed(containerRef.current, finalSpec, { renderer: 'svg', actions: false })
      .then((result) => {
        if (cancelled) {
          result.view.finalize();
          return;
        }
        viewRef.current = result.view;
        setReady(true);
      })
      .catch(() => {});
    return () => {
      // Finalize the view on unmount or before re-embedding with new data so
      // vega never touches a detached DOM node.
      cancelled = true;
      if (viewRef.current) {
        viewRef.current.finalize();
        viewRef.current = null;
      }
    };
  }, [hasData, finalSpec]);

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
    <div className={plain ? 'card analysis-card' : 'chart-card'}>
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
        <div ref={containerRef} style={{ width: '100%' }} />
      ) : (
        <div className="empty" style={{ padding: 26 }}>暂无数据</div>
      )}
    </div>
  );
}
