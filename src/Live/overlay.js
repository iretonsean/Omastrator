// Omastrator Live overlay (docs/OS-SUITE.md). Injected into the page over the
// DevTools Protocol: hover outline, click to select, Shift-click to add, and a
// compact bar next to the selection. Values go to Omastrator to snap to the
// project's tokens; it answers with applyResolved(). Nothing here writes files.
(() => {
  if (window.__oma) return;

  const send = (message) => {
    try { window.omastratorSend(JSON.stringify(message)); } catch (e) { /* not bound yet */ }
  };

  const styleProperties = ["color", "background-color", "padding", "margin", "padding-top", "padding-right", "padding-bottom", "padding-left",
    "margin-top", "margin-right", "margin-bottom", "margin-left", "width", "height", "font-size", "font-weight", "border-radius",
    "border-top-left-radius", "border-top-right-radius", "border-bottom-right-radius", "border-bottom-left-radius"];

  // Live inside a Browser View draws nothing: the app draws the boxes and the bar from what is reported here.
  let frameHost = window.__omaHost === "frame";

  const state = {
    enabled: !frameHost,
    selection: [],
    hover: null,
    tokens: { colors: [], spacing: [], fontSizes: [], fontWeights: [], radii: [] },
    ui: { background: "#1a1a1c", foreground: "#e5e5e7", accent: "#0a84ff" },
    panel: "",
    editing: null,
    handles: false,
    notice: "",
    // A site that isn't the user's: {origin, notice, sets: [{name, enabled, edits}], pending, suggested}.
    site: null,
    setsOpen: false
  };

  // Each element as it was before anything here changed it, so edits can be taken off again.
  const originals = new Map();
  // The inline style each element had before a scrub began.
  const previewed = new Map();

  // ------------------------------------------------------------ colours and lengths

  let canvas = null;
  function hex(value) {
    canvas = canvas || document.createElement("canvas");
    canvas.width = canvas.height = 1;
    const context = canvas.getContext("2d", { willReadFrequently: true });
    context.clearRect(0, 0, 1, 1);
    context.fillStyle = "#000";
    context.fillStyle = value;
    context.fillRect(0, 0, 1, 1);
    const [r, g, b, a] = context.getImageData(0, 0, 1, 1).data;
    const two = (n) => n.toString(16).padStart(2, "0");
    return "#" + two(r) + two(g) + two(b) + (a < 255 ? two(a) : "");
  }

  let probe = null;
  function pixels(value) {
    if (!probe) {
      probe = document.createElement("div");
      probe.style.cssText = "position:absolute;visibility:hidden;pointer-events:none;left:-9999px;top:0;";
    }
    (document.body || document.documentElement).appendChild(probe);
    probe.style.width = "";
    probe.style.width = value;
    const set = probe.style.width !== "";
    const px = set ? parseFloat(getComputedStyle(probe).width) : NaN;
    probe.remove();
    return set && !Number.isNaN(px) ? px : null;
  }

  function classify(raw) {
    if (/^-?\d*\.?\d+$/.test(raw)) return { kind: "number", value: raw };
    if (CSS.supports("color", raw)) return { kind: "color", value: raw, rgb: hex(raw) };
    const px = pixels(raw);
    if (px !== null) return { kind: "length", value: raw, px };
    return { kind: "other", value: raw };
  }

  // Every custom property the stylesheets declare, resolved as the root sees it.
  function scan() {
    const names = new Set();
    const visit = (rules) => {
      for (const rule of rules) {
        if (rule.style) for (let i = 0; i < rule.style.length; i++) if (rule.style[i].startsWith("--")) names.add(rule.style[i]);
        if (rule.cssRules) visit(rule.cssRules);
      }
    };
    for (const sheet of document.styleSheets) {
      try { visit(sheet.cssRules); } catch (e) { /* a cross-origin sheet */ }
    }
    const root = getComputedStyle(document.documentElement);
    const vars = {};
    for (const name of names) {
      const raw = root.getPropertyValue(name).trim();
      if (raw) vars[name] = classify(raw);
    }
    return { vars, rootFontSize: parseFloat(root.fontSize) || 16, url: location.href };
  }

  // ------------------------------------------------------------ elements

  function selectorFor(element) {
    const unique = (id) => document.querySelectorAll("#" + CSS.escape(id)).length === 1;
    if (element.id && unique(element.id)) return "#" + CSS.escape(element.id);
    const parts = [];
    for (let node = element; node && node.nodeType === 1 && node !== document.documentElement; node = node.parentElement) {
      if (node.id && unique(node.id)) { parts.unshift("#" + CSS.escape(node.id)); break; }
      let part = node.tagName.toLowerCase();
      const parent = node.parentElement;
      if (parent) {
        const same = Array.from(parent.children).filter((child) => child.tagName === node.tagName);
        if (same.length > 1) part += ":nth-of-type(" + (same.indexOf(node) + 1) + ")";
      }
      parts.unshift(part);
    }
    return parts.join(" > ");
  }

  function textOnly(element) {
    return element.children.length === 0 && element.textContent.trim().length > 0;
  }

  // The page as edit sets record it: its path, or a file page's name.
  function here() {
    return location.protocol === "file:" ? decodeURIComponent(location.pathname.split("/").pop()) : (location.pathname || "/");
  }

  function remember(element) {
    if (originals.has(element)) return;
    originals.set(element, {
      style: element.getAttribute("style"),
      cls: element.getAttribute("class"),
      text: element.children.length === 0 ? element.textContent : null
    });
  }

  function info(element) {
    const computed = getComputedStyle(element);
    const styles = {};
    for (const property of styleProperties) {
      const value = computed.getPropertyValue(property);
      styles[property] = property.endsWith("color") ? hex(value) : value;
    }
    const rect = element.getBoundingClientRect();
    return {
      selector: selectorFor(element),
      tag: element.tagName.toLowerCase(),
      id: element.id || "",
      classes: element.getAttribute("class") || "",
      text: textOnly(element) ? element.textContent : "",
      textOnly: textOnly(element),
      rect: { x: rect.x, y: rect.y, width: rect.width, height: rect.height },
      styles,
      inlineStyle: element.getAttribute("style") || "",
      path: here(),
      html: element.outerHTML.slice(0, 4000)
    };
  }

  // ------------------------------------------------------------ the layer

  const host = document.createElement("div");
  host.id = "omastrator-overlay";
  host.style.cssText = "position:fixed;inset:0;z-index:2147483647;pointer-events:none;";
  const root = host.attachShadow({ mode: "open" });
  root.innerHTML = `
    <style>
      :host { all: initial; }
      * { box-sizing: border-box; font: 12px/1.3 system-ui, sans-serif; }
      .box { position: fixed; pointer-events: none; border: 1px solid var(--accent); }
      .hover { border-style: dashed; opacity: .8; display: none; }
      .panel:empty, .notice:empty { display: none; }
      .selected { border-width: 2px; }
      .bar { position: fixed; pointer-events: auto; display: none; flex-direction: column; gap: 6px; padding: 6px;
             border-radius: 10px; background: var(--bg); color: var(--fg); box-shadow: 0 8px 28px rgba(0,0,0,.35);
             border: 1px solid color-mix(in srgb, var(--fg) 18%, transparent); max-width: 460px; }
      .row { display: flex; gap: 2px; flex-wrap: wrap; }
      button { border: 0; background: transparent; color: var(--fg); padding: 4px 8px; border-radius: 6px; cursor: pointer; }
      button:hover, button.on { background: color-mix(in srgb, var(--accent) 30%, transparent); }
      .panel { display: flex; flex-wrap: wrap; gap: 6px; align-items: center; }
      .chip { width: 18px; height: 18px; border-radius: 4px; padding: 0; border: 1px solid color-mix(in srgb, var(--fg) 30%, transparent); }
      .token { font-size: 11px; padding: 2px 6px; border: 1px solid color-mix(in srgb, var(--fg) 25%, transparent); }
      input, select, textarea { background: color-mix(in srgb, var(--fg) 8%, var(--bg)); color: var(--fg); border-radius: 6px;
             border: 1px solid color-mix(in srgb, var(--fg) 25%, transparent); padding: 3px 6px; }
      input[type=number] { width: 64px; }
      textarea { width: 100%; min-height: 48px; resize: vertical; }
      label { opacity: .75; }
      .notice { opacity: .8; font-size: 11px; }
      .handle { position: fixed; pointer-events: auto; background: var(--accent); opacity: .75; border-radius: 2px; }
      .handle.margin { background: #f5a524; }
      .handle.x { cursor: ew-resize; width: 6px; }
      .handle.y { cursor: ns-resize; height: 6px; }
      .site { position: fixed; right: 12px; bottom: 12px; pointer-events: auto; display: none; flex-direction: column; gap: 6px;
              padding: 8px; border-radius: 10px; background: var(--bg); color: var(--fg); max-width: 520px;
              box-shadow: 0 8px 28px rgba(0,0,0,.35); border: 1px solid color-mix(in srgb, var(--accent) 60%, transparent); }
      .site-line { font-weight: 600; }
      .site-count:empty, .site-sets:empty { display: none; }
      .site-sets label { display: block; opacity: 1; }
    </style>
    <div class="box hover"></div>
    <div class="selections"></div>
    <div class="handles"></div>
    <div class="bar">
      <div class="row">
        <button data-panel="text">Text</button>
        <button data-panel="color">Colour</button>
        <button data-panel="background">Background</button>
        <button data-panel="spacing">Spacing</button>
        <button data-panel="size">Size</button>
        <button data-panel="type">Type</button>
        <button data-panel="radius">Radius</button>
        <button data-panel="ask">Ask AI…</button>
      </div>
      <div class="panel"></div>
      <div class="notice"></div>
    </div>
    <div class="site" role="region" aria-label="Not your site">
      <div class="site-line">Not your site: changes stay on this machine.</div>
      <div class="site-count"></div>
      <div class="row">
        <input class="site-name" aria-label="Edit set name" placeholder="Edit set name">
        <button data-site="keep">Keep Edits</button>
        <button data-site="sets">Edit Sets</button>
        <button data-site="export">Export CSS…</button>
        <button data-site="beforeAfter">Before and After to Desk</button>
        <button data-site="handoff">Hand to Agent…</button>
      </div>
      <div class="site-sets"></div>
    </div>`;
  const hoverBox = root.querySelector(".hover");
  const selections = root.querySelector(".selections");
  const handleLayer = root.querySelector(".handles");
  const bar = root.querySelector(".bar");
  const panel = root.querySelector(".panel");
  const notice = root.querySelector(".notice");
  const site = root.querySelector(".site");
  const siteCount = root.querySelector(".site-count");
  const siteName = root.querySelector(".site-name");
  const siteSets = root.querySelector(".site-sets");

  function drawSite() {
    const about = state.site;
    site.style.display = about ? "flex" : "none";
    if (!about) return;
    const on = (about.sets || []).filter((set) => set.enabled);
    const parts = [];
    if (about.pending) parts.push(about.pending + (about.pending === 1 ? " edit not kept yet" : " edits not kept yet"));
    if (on.length) parts.push("Showing " + on.map((set) => set.name).join(", "));
    siteCount.textContent = parts.join(". ");
    if (about.suggested) siteName.placeholder = about.suggested;
    siteSets.innerHTML = "";
    if (!state.setsOpen) return;
    if (!(about.sets || []).length) siteSets.textContent = "No edit sets for this site yet.";
    for (const set of about.sets || []) {
      const row = document.createElement("label");
      const box = document.createElement("input");
      box.type = "checkbox";
      box.checked = !!set.enabled;
      box.addEventListener("change", () => send({ type: "site", action: "toggle", name: set.name, on: box.checked }));
      row.appendChild(box);
      row.appendChild(document.createTextNode(" " + set.name + " (" + set.edits + ")"));
      siteSets.appendChild(row);
    }
  }

  for (const button of root.querySelectorAll("[data-site]")) {
    button.addEventListener("click", () => {
      const action = button.dataset.site;
      if (action === "sets") { state.setsOpen = !state.setsOpen; drawSite(); return; }
      send({ type: "site", action, name: siteName.value.trim() || (state.site && state.site.suggested) || "" });
      if (action === "keep") siteName.value = "";
    });
  }
  siteName.addEventListener("keydown", (event) => {
    event.stopPropagation();
    if (event.key === "Enter") root.querySelector('[data-site="keep"]').click();
  });

  // Edits put back when the page is visited again. Elements a framework renders late are caught for a few seconds.
  let late = [];
  let lateWatch = null;
  function applyOne(edit) {
    if (edit.path && edit.path !== here()) return true;
    let element = null;
    try { element = document.querySelector(edit.selector); } catch (e) { return true; }
    if (!element) return false;
    remember(element);
    if (edit.property === "text") {
      if (element.children.length === 0) element.textContent = edit.value;
    } else {
      if (edit.removeClass) element.classList.remove(edit.removeClass);
      if (edit.addClass) element.classList.add(edit.addClass);
      element.style.setProperty(edit.property, edit.value);
    }
    return true;
  }
  function watchLate() {
    if (lateWatch || !late.length) return;
    lateWatch = new MutationObserver(() => {
      late = late.filter((edit) => !applyOne(edit));
      if (!late.length) { lateWatch.disconnect(); lateWatch = null; }
    });
    lateWatch.observe(document.documentElement, { childList: true, subtree: true });
    setTimeout(() => { if (lateWatch) { lateWatch.disconnect(); lateWatch = null; late = []; } }, 10000);
  }

  function attach() {
    if (!left && !frameHost && !host.isConnected) document.documentElement.appendChild(host);
  }

  // Frame host: the hover and selection boxes in page px, once per animation frame while anything moves.
  let reportQueued = false;
  let lastReport = "";
  function report() {
    if (!frameHost || reportQueued) return;
    reportQueued = true;
    const run = () => {
      if (!reportQueued) return;
      reportQueued = false;
      const box = (element) => {
        const rect = element.getBoundingClientRect();
        return { selector: selectorFor(element), tag: element.tagName.toLowerCase(), rect: { x: rect.x, y: rect.y, width: rect.width, height: rect.height } };
      };
      const hover = state.enabled && state.hover && state.hover.isConnected ? box(state.hover) : null;
      const payload = { type: "geometry", hover, selection: state.selection.filter((e) => e.isConnected).map(box),
        scroll: { x: scrollX, y: scrollY }, viewport: { width: innerWidth, height: innerHeight } };
      const text = JSON.stringify(payload);
      if (text === lastReport) return;
      lastReport = text;
      send(payload);
    };
    requestAnimationFrame(run);
    setTimeout(run, 100);
  }

  function paintTheme() {
    host.style.setProperty("--bg", state.ui.background);
    host.style.setProperty("--fg", state.ui.foreground);
    host.style.setProperty("--accent", state.ui.accent);
  }

  function place(box, element) {
    const rect = element.getBoundingClientRect();
    box.style.left = rect.left + "px";
    box.style.top = rect.top + "px";
    box.style.width = rect.width + "px";
    box.style.height = rect.height + "px";
  }

  function primary() {
    return state.selection[0] || null;
  }

  function redraw() {
    if (frameHost) { report(); return; }
    selections.innerHTML = "";
    for (const element of state.selection) {
      const box = document.createElement("div");
      box.className = "box selected";
      place(box, element);
      selections.appendChild(box);
    }
    const first = primary();
    bar.style.display = first ? "flex" : "none";
    if (first) {
      const rect = first.getBoundingClientRect();
      const below = rect.bottom + 8;
      const height = bar.offsetHeight || 40;
      const top = below + height < innerHeight ? below : Math.max(8, rect.top - height - 8);
      bar.style.top = top + "px";
      bar.style.left = Math.max(8, Math.min(rect.left, innerWidth - bar.offsetWidth - 8)) + "px";
    }
    drawHandles();
    notice.textContent = state.notice;
  }

  // ------------------------------------------------------------ selecting

  function inOverlay(event) {
    return event.composedPath().includes(host);
  }

  function select(element, add) {
    if (!element) return;
    stopEditing();
    if (add) {
      const at = state.selection.indexOf(element);
      if (at >= 0) state.selection.splice(at, 1); else state.selection.push(element);
    } else {
      state.selection = [element];
    }
    state.panel = "";
    state.notice = "";
    renderPanel();
    redraw();
    send({ type: "select", elements: state.selection.map(info) });
  }

  function clear() {
    stopEditing();
    state.selection = [];
    state.panel = "";
    renderPanel();
    redraw();
    send({ type: "select", elements: [] });
  }

  addEventListener("mousemove", (event) => {
    if (frameHost) {
      if (!state.enabled) return;
      state.hover = event.target instanceof Element ? event.target : null;
      report();
      return;
    }
    if (!state.enabled || inOverlay(event) || state.dragging) { hoverBox.style.display = "none"; return; }
    const target = event.target;
    if (!(target instanceof Element) || target === state.editing) return;
    hoverBox.style.display = "block";
    place(hoverBox, target);
  }, true);

  const swallow = (event) => {
    if (!state.enabled || inOverlay(event)) return;
    if (state.editing && state.editing.contains(event.target)) return;
    event.preventDefault();
    event.stopPropagation();
  };
  addEventListener("mousedown", swallow, true);
  addEventListener("mouseup", swallow, true);
  addEventListener("click", (event) => {
    if (!state.enabled || inOverlay(event)) return;
    if (state.editing && state.editing.contains(event.target)) return;
    event.preventDefault();
    event.stopPropagation();
    if (event.target instanceof Element) select(event.target, event.shiftKey);
  }, true);
  addEventListener("submit", (event) => { if (state.enabled) event.preventDefault(); }, true);
  addEventListener("keydown", (event) => {
    if (event.key === "Escape" && state.enabled && !state.editing) clear();
  }, true);
  addEventListener("scroll", redraw, true);
  addEventListener("resize", redraw);

  // ------------------------------------------------------------ editing

  // Asks Omastrator to snap a value; it answers with applyResolved().
  function request(property, value) {
    for (const element of state.selection) {
      send({ type: "edit", selector: selectorFor(element), property, value: String(value), element: info(element) });
    }
  }

  function preview(property, value) {
    for (const element of state.selection) { remember(element); element.style.setProperty(property, value); }
    redraw();
  }

  function stopEditing() {
    const element = state.editing;
    if (!element) return;
    state.editing = null;
    element.contentEditable = "inherit";
    const before = element.dataset.omaTextBefore || "";
    delete element.dataset.omaTextBefore;
    if (element.textContent !== before)
      send({ type: "edit", selector: selectorFor(element), property: "text", value: element.textContent, before, element: state.editingBefore });
  }

  function startEditing() {
    const element = primary();
    if (!element || !textOnly(element)) {
      state.notice = "Only an element that holds text alone can be edited in place.";
      redraw();
      return;
    }
    remember(element);
    state.editing = element;
    state.editingBefore = info(element);
    element.dataset.omaTextBefore = element.textContent;
    element.contentEditable = "plaintext-only";
    element.focus();
    element.addEventListener("keydown", (event) => {
      if (event.key === "Enter" && !event.shiftKey) { event.preventDefault(); stopEditing(); }
      if (event.key === "Escape") { element.textContent = element.dataset.omaTextBefore || element.textContent; stopEditing(); }
    });
    element.addEventListener("blur", stopEditing, { once: true });
  }

  function chips(tokens, property, isColor) {
    const fragment = document.createDocumentFragment();
    for (const token of tokens.slice(0, 40)) {
      const chip = document.createElement("button");
      chip.className = isColor ? "chip" : "token";
      chip.title = token.name + " " + token.value;
      if (isColor) chip.style.background = token.value; else chip.textContent = token.name.replace(/^--/, "").replace(/^spacing /, "");
      chip.addEventListener("click", () => request(property, token.value));
      fragment.appendChild(chip);
    }
    return fragment;
  }

  function field(label, value, onCommit, type = "number") {
    const wrap = document.createElement("label");
    wrap.textContent = label + " ";
    const input = document.createElement("input");
    input.type = type;
    input.value = value;
    input.addEventListener("change", () => onCommit(input.value));
    wrap.appendChild(input);
    return wrap;
  }

  function renderPanel() {
    panel.innerHTML = "";
    for (const button of root.querySelectorAll("[data-panel]")) button.classList.toggle("on", button.dataset.panel === state.panel);
    const element = primary();
    state.handles = state.panel === "spacing";
    if (!element || !state.panel) { drawHandles(); return; }
    const computed = getComputedStyle(element);
    if (state.panel === "color" || state.panel === "background") {
      const property = state.panel === "color" ? "color" : "background-color";
      const picker = document.createElement("input");
      picker.type = "color";
      picker.value = hex(computed.getPropertyValue(property)).slice(0, 7);
      picker.addEventListener("input", () => preview(property, picker.value));
      picker.addEventListener("change", () => request(property, picker.value));
      panel.appendChild(picker);
      panel.appendChild(chips(state.tokens.colors || [], property, true));
    } else if (state.panel === "spacing") {
      for (const side of ["top", "right", "bottom", "left"]) {
        panel.appendChild(field("padding " + side, parseFloat(computed.getPropertyValue("padding-" + side)) || 0,
          (v) => request("padding-" + side, v + "px")));
      }
      panel.appendChild(document.createTextNode("Drag the blue handles for padding, the amber ones for margin."));
    } else if (state.panel === "size") {
      panel.appendChild(field("W", Math.round(element.getBoundingClientRect().width), (v) => request("width", v + "px")));
      panel.appendChild(field("H", Math.round(element.getBoundingClientRect().height), (v) => request("height", v + "px")));
    } else if (state.panel === "type") {
      panel.appendChild(field("Size", parseFloat(computed.fontSize), (v) => request("font-size", v + "px")));
      const weight = document.createElement("select");
      for (const w of [100, 200, 300, 400, 500, 600, 700, 800, 900]) {
        const option = document.createElement("option");
        option.value = option.textContent = String(w);
        option.selected = String(w) === computed.fontWeight;
        weight.appendChild(option);
      }
      weight.addEventListener("change", () => request("font-weight", weight.value));
      panel.appendChild(weight);
      panel.appendChild(chips(state.tokens.fontSizes || [], "font-size", false));
    } else if (state.panel === "radius") {
      panel.appendChild(field("Radius", parseFloat(computed.borderTopLeftRadius) || 0, (v) => request("border-radius", v + "px")));
      panel.appendChild(chips(state.tokens.radii || [], "border-radius", false));
    } else if (state.panel === "ask") {
      const text = document.createElement("textarea");
      text.placeholder = "What should change? For example: make this card match the one above.";
      const go = document.createElement("button");
      go.textContent = "Send";
      go.className = "on";
      go.addEventListener("click", () => {
        if (!text.value.trim()) return;
        send({ type: "ask", prompt: text.value.trim(), elements: state.selection.map(info) });
        text.value = "";
      });
      panel.appendChild(text);
      panel.appendChild(go);
    }
    drawHandles();
  }

  for (const button of root.querySelectorAll("[data-panel]")) {
    button.addEventListener("click", () => {
      if (button.dataset.panel === "text") { state.panel = ""; renderPanel(); startEditing(); return; }
      state.panel = state.panel === button.dataset.panel ? "" : button.dataset.panel;
      renderPanel();
      redraw();
    });
  }

  // Padding handles sit inside the element's edges, margin handles outside; dragging previews, release snaps.
  function drawHandles() {
    handleLayer.innerHTML = "";
    const element = primary();
    if (!element || !state.handles) return;
    const rect = element.getBoundingClientRect();
    const computed = getComputedStyle(element);
    for (const kind of ["padding", "margin"]) {
      for (const side of ["top", "right", "bottom", "left"]) {
        const amount = parseFloat(computed.getPropertyValue(kind + "-" + side)) || 0;
        const handle = document.createElement("div");
        const vertical = side === "top" || side === "bottom";
        handle.className = "handle " + kind + " " + (vertical ? "y" : "x");
        const inset = kind === "padding" ? amount : -amount - 6;
        if (vertical) {
          handle.style.width = Math.max(16, rect.width / 4) + "px";
          handle.style.left = rect.left + rect.width / 2 - Math.max(16, rect.width / 4) / 2 + "px";
          handle.style.top = (side === "top" ? rect.top + inset : rect.bottom - inset - 6) + "px";
        } else {
          handle.style.height = Math.max(16, rect.height / 4) + "px";
          handle.style.top = rect.top + rect.height / 2 - Math.max(16, rect.height / 4) / 2 + "px";
          handle.style.left = (side === "left" ? rect.left + inset : rect.right - inset - 6) + "px";
        }
        handle.title = kind + "-" + side + ": " + amount + "px";
        handle.addEventListener("mousedown", (down) => {
          down.preventDefault();
          down.stopPropagation();
          state.dragging = true;
          const property = kind + "-" + side;
          const start = { x: down.clientX, y: down.clientY, amount };
          let value = amount;
          const move = (event) => {
            const delta = vertical ? event.clientY - start.y : event.clientX - start.x;
            const inward = (side === "top" || side === "left") ? 1 : -1;
            value = Math.max(0, Math.round(start.amount + delta * inward * (kind === "padding" ? 1 : -1)));
            preview(property, value + "px");
          };
          const up = () => {
            removeEventListener("mousemove", move, true);
            removeEventListener("mouseup", up, true);
            state.dragging = false;
            request(property, value + "px");
          };
          addEventListener("mousemove", move, true);
          addEventListener("mouseup", up, true);
        });
        handleLayer.appendChild(handle);
      }
    }
  }

  // ------------------------------------------------------------ what Omastrator calls

  // Live left a tab it didn't open (the user's own Chromium): the page goes back to how it behaves without Omastrator.
  // The listeners stay but do nothing; a later Live injects a fresh overlay.
  let left = false;
  function leave() {
    left = true;
    state.enabled = false;
    state.selection = [];
    host.remove();
    if (window.__oma === api) delete window.__oma;
  }

  const api = window.__oma = {
    version: 1,
    leave,
    scan,
    info: (selector) => { const element = document.querySelector(selector); return element ? info(element) : null; },
    selectorFor,
    setTokens(tokens, ui) {
      state.tokens = tokens || state.tokens;
      if (ui) state.ui = Object.assign({}, state.ui, ui);
      paintTheme();
      renderPanel();
    },
    enable(on) {
      state.enabled = !!on;
      if (!on) { state.hover = null; clear(); hoverBox.style.display = "none"; }
      report();
    },
    // "frame": inside a Browser View. Nothing is drawn and the page is plain until Edit Page enables it.
    setHost(name) {
      frameHost = name === "frame";
      if (frameHost) { host.remove(); state.enabled = false; state.hover = null; }
      else attach();
      report();
    },
    // Puts one element back as it was: {style, cls, text} with null for "no attribute" or "leave the text".
    restore(selector, was) {
      let element = null;
      try { element = document.querySelector(selector); } catch (e) { return false; }
      if (!element) return false;
      remember(element);
      if (was.style === null || was.style === "") element.removeAttribute("style"); else element.setAttribute("style", was.style);
      if (was.cls === null || was.cls === "") element.removeAttribute("class"); else element.setAttribute("class", was.cls);
      if (was.text !== null && was.text !== undefined && element.children.length === 0) element.textContent = was.text;
      redraw();
      return true;
    },
    select(selector, add) {
      const element = document.querySelector(selector);
      if (element) select(element, !!add);
      return element ? info(element) : null;
    },
    clear,
    request,
    // Replaces an element's text as editing it in place does.
    editText(selector, text) {
      const element = document.querySelector(selector);
      if (!element || !textOnly(element)) return false;
      const before = info(element);
      remember(element);
      element.textContent = text;
      send({ type: "edit", selector: selectorFor(element), property: "text", value: text, before: before.text, element: before });
      redraw();
      return true;
    },
    selection: () => state.selection.map(info),
    // A scrub in Omastrator's element bar: shown on each selected element, and taken off before the edit that records it.
    previewSelected(properties, value) {
      for (const element of state.selection) {
        if (!previewed.has(element)) previewed.set(element, element.getAttribute("style"));
        for (const property of properties) element.style.setProperty(property, value);
      }
      redraw();
    },
    endPreview() {
      for (const [element, style] of previewed) {
        if (style === null) element.removeAttribute("style"); else element.setAttribute("style", style);
      }
      previewed.clear();
      redraw();
    },
    notice(text) { state.notice = text || ""; redraw(); },
    // Omastrator's answer to an edit: a class swap and, where the page lacks that class's CSS, the value inline.
    applyResolved(resolution) {
      const element = document.querySelector(resolution.selector);
      if (!element) return null;
      const before = info(element);
      remember(element);
      if (resolution.property === "text") {
        element.textContent = resolution.value;
      } else {
        if (resolution.removeClass) element.classList.remove(resolution.removeClass);
        if (resolution.addClass) element.classList.add(resolution.addClass);
        element.style.removeProperty(resolution.property);
        const want = resolution.property.endsWith("color") ? hex(resolution.value) : resolution.value;
        const now = getComputedStyle(element).getPropertyValue(resolution.property);
        const have = resolution.property.endsWith("color") ? hex(now) : now;
        if (!resolution.addClass || have !== want) element.style.setProperty(resolution.property, resolution.value);
      }
      redraw();
      return { before, after: info(element) };
    },
    // A site that isn't the user's: the label and its edit sets; null for the user's own.
    setSite(about) {
      state.site = about || null;
      drawSite();
    },
    // Puts edits back: [{path, selector, property, value, addClass, removeClass}]. Returns how many found their element.
    applyEdits(list) {
      const all = list || [];
      late = all.filter((edit) => !applyOne(edit));
      watchLate();
      redraw();
      return all.length - late.length;
    },
    // Takes every change off the page, so it looks as the site made it.
    revertAll() {
      if (lateWatch) { lateWatch.disconnect(); lateWatch = null; }
      late = [];
      for (const [element, was] of originals) {
        if (was.style === null) element.removeAttribute("style"); else element.setAttribute("style", was.style);
        if (was.cls === null) element.removeAttribute("class"); else element.setAttribute("class", was.cls);
        if (was.text !== null && element.children.length === 0) element.textContent = was.text;
      }
      const count = originals.size;
      originals.clear();
      redraw();
      return count;
    },
    here
  };

  paintTheme();
  // Injected before the page parses, the layer waits for its root; frameworks that replace the page keep it on top.
  const watch = () => {
    attach();
    new MutationObserver(() => { if (!host.isConnected) attach(); }).observe(document.documentElement, { childList: true });
  };
  if (document.documentElement) watch();
  else document.addEventListener("readystatechange", watch, { once: true });
})();
