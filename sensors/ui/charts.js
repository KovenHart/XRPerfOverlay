// Courbes en SVG : relevé minute par minute des rapports, petites courbes du tableau de bord, anneau.
// Pas de bibliothèque : tout est dessiné ici, aux couleurs du thème (variables CSS).
"use strict";

window.Charts = (function () {
  const NS = "http://www.w3.org/2000/svg";

  function svg(tag, attrs) {
    const el = document.createElementNS(NS, tag);
    for (const k in attrs || {}) el.setAttribute(k, attrs[k]);
    return el;
  }

  // Graduations « rondes » : 0, 10, 20... ou 0, 2.5, 5...
  function niceTicks(min, max, count) {
    const span = Math.max(1e-9, max - min);
    const raw = span / Math.max(1, count);
    const pow = Math.pow(10, Math.floor(Math.log10(raw)));
    const steps = [1, 2, 2.5, 5, 10];
    let step = pow * 10;
    for (const s of steps) { if (s * pow >= raw) { step = s * pow; break; } }
    const start = Math.floor(min / step + 1e-9) * step;
    const end = Math.ceil(max / step - 1e-9) * step;
    const ticks = [];
    for (let v = start; v <= end + step * 0.001; v += step) ticks.push(+v.toFixed(6));
    return { ticks, step, min: start, max: end };
  }

  function fmt(v, digits) {
    if (v == null || isNaN(v)) return "--";
    return window.App ? window.App.num(v, digits) : v.toFixed(digits);
  }

  const tip = () => document.getElementById("tooltip");

  function showTip(evt, title, rows) {
    const t = tip();
    t.replaceChildren();
    const head = document.createElement("div");
    head.className = "tt-title";
    head.textContent = title;
    t.appendChild(head);
    for (const r of rows) {
      const row = document.createElement("div");
      row.className = "tt-row";
      const key = document.createElement("i");
      key.style.background = r.color;
      const b = document.createElement("b");
      b.textContent = r.value;
      const s = document.createElement("span");
      s.textContent = r.name;
      row.append(key, b, s);
      t.appendChild(row);
    }
    t.hidden = false;
    const pad = 14, w = t.offsetWidth, h = t.offsetHeight;
    let x = evt.clientX + pad, y = evt.clientY + pad;
    if (x + w > window.innerWidth - 8) x = evt.clientX - w - pad;
    if (y + h > window.innerHeight - 8) y = evt.clientY - h - pad;
    t.style.left = x + "px";
    t.style.top = y + "px";
  }

  function hideTip() { tip().hidden = true; }

  /**
   * Courbe d'une ou plusieurs séries sur un seul axe.
   * opts : { title, unit, digits, series: [{ name, color, values, dashed }], labels: [x...], xLabel(i),
   *          min, max, refs: [{ value, label }], height, empty }
   */
  function line(opts) {
    const box = document.createElement("section");
    box.className = "chart";
    const head = document.createElement("div");
    head.className = "chart-head";
    const h3 = document.createElement("h3");
    h3.textContent = opts.title + (opts.unit ? " (" + opts.unit + ")" : "");
    head.appendChild(h3);
    const shown = opts.series.filter(s => s.values.some(v => v != null && !isNaN(v)) || s.legendOnly);
    if (shown.length > 1 || opts.forceLegend) {
      const legend = document.createElement("div");
      legend.className = "legend";
      for (const s of shown) {
        const item = document.createElement("span");
        const key = document.createElement("i");
        key.style.background = s.color;
        if (s.dashed) { key.className = "dash"; key.style.color = s.color; key.style.background = "none"; }
        const name = document.createElement("span");
        name.textContent = s.name;
        item.append(key, name);
        legend.appendChild(item);
      }
      head.appendChild(legend);
    }
    box.appendChild(head);

    const n = opts.labels.length;
    const plotted = shown.filter(s => !s.legendOnly);
    if (n < 2 || plotted.length === 0) {
      const empty = document.createElement("div");
      empty.className = "chart-empty";
      empty.textContent = opts.empty || "";
      box.appendChild(empty);
      return box;
    }

    const holder = document.createElement("div");
    box.appendChild(holder);
    const height = opts.height || 170;

    function draw() {
      const width = Math.max(240, holder.clientWidth || 600);
      let lo = opts.min != null ? opts.min : Infinity, hi = opts.max != null ? opts.max : -Infinity;
      for (const s of plotted) for (const v of s.values) if (v != null && !isNaN(v)) { lo = Math.min(lo, v); hi = Math.max(hi, v); }
      for (const r of opts.refs || []) if (r.value != null && !isNaN(r.value)) hi = Math.max(hi, r.value);
      if (opts.min == null) lo = Math.min(lo, 0);
      if (!(hi > lo)) hi = lo + 1;
      // Haut de l'axe : la valeur la plus haute (ou la référence) avec un peu d'air, arrondie à une graduation.
      const scale = opts.max != null ? niceTicks(lo, hi, 4) : niceTicks(lo, hi + (hi - lo) * 0.04, 4);
      const left = 40, right = 12, top = 10, axis = 24;
      const pw = width - left - right, ph = height;
      const x = i => left + (n === 1 ? pw / 2 : (pw * i) / (n - 1));
      const y = v => top + ph - ((v - scale.min) / (scale.max - scale.min)) * ph;

      const root = svg("svg", { viewBox: `0 0 ${width} ${top + ph + axis}`, height: top + ph + axis, role: "img", "aria-label": opts.title });
      const grid = svg("g", { class: "axis" });
      for (const t of scale.ticks) {
        const yy = Math.round(y(t)) + 0.5;
        grid.appendChild(svg("line", { class: "gridline", x1: left, x2: width - right, y1: yy, y2: yy }));
        const label = svg("text", { x: left - 8, y: yy + 4, "text-anchor": "end" });
        label.textContent = fmt(t, scale.step < 1 ? 1 : 0);
        grid.appendChild(label);
      }
      // Abscisse : une étiquette toutes les 1, 5, 10, 15 ou 30 minutes selon la longueur.
      const every = n <= 12 ? 1 : n <= 60 ? 5 : n <= 120 ? 10 : n <= 240 ? 15 : 30;
      let lastX = -1e9;
      for (let i = 0; i < n; i++) {
        const m = opts.labels[i];
        if (i !== 0 && m % every !== 0) continue;
        const xx = x(i);
        if (xx - lastX < 34) continue;
        lastX = xx;
        const label = svg("text", { x: xx, y: top + ph + 17, "text-anchor": i === 0 ? "start" : "middle" });
        label.textContent = opts.xLabel ? opts.xLabel(m, i) : m;
        grid.appendChild(label);
      }
      root.appendChild(grid);

      for (const r of opts.refs || []) {
        if (r.value == null || isNaN(r.value) || r.value < scale.min || r.value > scale.max) continue;
        const yy = y(r.value);
        root.appendChild(svg("line", { class: "ref", x1: left, x2: width - right, y1: yy, y2: yy }));
        const label = svg("text", { class: "ref-label", x: width - right, y: yy - 5, "text-anchor": "end" });
        label.textContent = r.label;
        root.appendChild(label);
      }

      for (const s of plotted) {
        // Courbe interrompue là où la valeur manque.
        let d = "", open = false;
        s.values.forEach((v, i) => {
          if (v == null || isNaN(v)) { open = false; return; }
          d += (open ? "L" : "M") + x(i).toFixed(1) + "," + y(v).toFixed(1);
          open = true;
        });
        if (!d) continue;
        const path = svg("path", { class: "series", d, stroke: s.color });
        if (s.dashed) path.setAttribute("stroke-dasharray", "5 4");
        root.appendChild(path);
        if (opts.animate) {
          const len = path.getTotalLength ? path.getTotalLength() : 0;
          if (len) {
            path.style.strokeDasharray = s.dashed ? "" : len;
            if (!s.dashed) {
              path.style.strokeDashoffset = len;
              path.animate([{ strokeDashoffset: len }, { strokeDashoffset: 0 }], { duration: 700, easing: "cubic-bezier(.2,.7,.2,1)", fill: "forwards" });
            }
          }
        }
      }

      // Survol : un trait vertical suit la souris et s'arrête sur la minute la plus proche.
      const cross = svg("line", { class: "cross", y1: top, y2: top + ph, visibility: "hidden" });
      root.appendChild(cross);
      const dots = plotted.map(s => {
        const c = svg("circle", { r: 4, fill: s.color, stroke: "var(--surface)", "stroke-width": 2, visibility: "hidden" });
        root.appendChild(c);
        return c;
      });
      const hit = svg("rect", { class: "hit", x: left, y: top, width: pw, height: ph, tabindex: 0 });
      root.appendChild(hit);
      let focusIndex = n - 1;
      const at = (i, evt) => {
        i = Math.max(0, Math.min(n - 1, i));
        focusIndex = i;
        const xx = x(i);
        cross.setAttribute("x1", xx); cross.setAttribute("x2", xx); cross.setAttribute("visibility", "visible");
        const rows = [];
        plotted.forEach((s, k) => {
          const v = s.values[i];
          if (v == null || isNaN(v)) { dots[k].setAttribute("visibility", "hidden"); return; }
          dots[k].setAttribute("cx", xx); dots[k].setAttribute("cy", y(v)); dots[k].setAttribute("visibility", "visible");
          rows.push({ name: s.name, color: s.color, value: fmt(v, opts.digits != null ? opts.digits : 1) + (opts.unit ? " " + opts.unit : "") });
        });
        const title = opts.tipTitle ? opts.tipTitle(opts.labels[i], i) : String(opts.labels[i]);
        if (evt) showTip(evt, title, rows);
      };
      const clear = () => { cross.setAttribute("visibility", "hidden"); dots.forEach(d => d.setAttribute("visibility", "hidden")); hideTip(); };
      hit.addEventListener("pointermove", e => {
        const r = hit.getBoundingClientRect();
        const rel = (e.clientX - r.left) / r.width;
        at(Math.round(rel * (n - 1)), e);
      });
      hit.addEventListener("pointerleave", clear);
      hit.addEventListener("blur", clear);
      hit.addEventListener("keydown", e => {
        if (e.key !== "ArrowLeft" && e.key !== "ArrowRight") return;
        e.preventDefault();
        const i = focusIndex + (e.key === "ArrowRight" ? 1 : -1);
        const r = hit.getBoundingClientRect();
        const xx = r.left + (r.width * Math.max(0, Math.min(n - 1, i))) / (n - 1);
        at(i, { clientX: xx, clientY: r.top + 10 });
      });

      holder.replaceChildren(root);
      opts.animate = false; // seulement au premier dessin
    }

    requestAnimationFrame(draw);
    let lastWidth = 0;
    const ro = new ResizeObserver(() => {
      const w = holder.clientWidth;
      if (Math.abs(w - lastWidth) > 2) { lastWidth = w; draw(); }
    });
    ro.observe(holder);
    return box;
  }

  /** Petite courbe d'une seule série (tableau de bord), sans axes. */
  function spark(values, color, min, max) {
    const width = 200, height = 46;
    const root = svg("svg", { class: "spark", viewBox: `0 0 ${width} ${height}`, preserveAspectRatio: "none", "aria-hidden": "true" });
    const known = values.filter(v => v != null && !isNaN(v));
    if (known.length < 2) return root;
    let lo = min != null ? min : Math.min(...known), hi = max != null ? max : Math.max(...known);
    if (!(hi > lo)) hi = lo + 1;
    const n = values.length;
    const x = i => (width * i) / Math.max(1, n - 1);
    const y = v => 3 + (height - 6) * (1 - (Math.min(hi, Math.max(lo, v)) - lo) / (hi - lo));
    let d = "", open = false, first = null, last = null;
    values.forEach((v, i) => {
      if (v == null || isNaN(v)) { open = false; return; }
      d += (open ? "L" : "M") + x(i).toFixed(1) + "," + y(v).toFixed(1);
      if (first == null) first = i;
      last = i;
      open = true;
    });
    const area = svg("path", { d: d + `L${x(last)},${height}L${x(first)},${height}Z`, fill: color, opacity: 0.1 });
    const path = svg("path", { d, fill: "none", stroke: color, "stroke-width": 2, "vector-effect": "non-scaling-stroke", "stroke-linejoin": "round" });
    root.append(area, path);
    return root;
  }

  /** Anneau de progression (part de la cible tenue). */
  function ring(percent, color, big, small) {
    const wrap = document.createElement("div");
    wrap.className = "ring";
    const r = 52, c = 2 * Math.PI * r;
    const root = svg("svg", { viewBox: "0 0 120 120", "aria-hidden": "true" });
    root.appendChild(svg("circle", { cx: 60, cy: 60, r, fill: "none", stroke: "var(--line)", "stroke-width": 10 }));
    const p = Math.max(0, Math.min(100, percent || 0));
    const arc = svg("circle", { cx: 60, cy: 60, r, fill: "none", stroke: color, "stroke-width": 10, "stroke-linecap": "round", "stroke-dasharray": `${(c * p) / 100} ${c}` });
    if (p > 0) root.appendChild(arc);
    if (p > 0 && !document.documentElement.classList.contains("no-anim")) {
      arc.animate([{ strokeDasharray: `0 ${c}` }, { strokeDasharray: `${(c * p) / 100} ${c}` }], { duration: 800, easing: "cubic-bezier(.2,.7,.2,1)" });
    }
    const v = document.createElement("div");
    v.className = "v";
    const b = document.createElement("b");
    b.textContent = big;
    const s = document.createElement("span");
    s.textContent = small;
    v.append(b, s);
    wrap.append(root, v);
    return wrap;
  }

  return { line, spark, ring, showTip, hideTip };
})();
