// Omastrator Live: motion (docs/MOTION.md, section 1). Reads the page's animations through the Web Animations API and
// pauses, seeks and releases them for the timeline. It runs after overlay.js, which it extends as window.__oma.motion.
// Nothing here writes files or changes the page's code.
(() => {
  const oma = window.__oma;
  if (!oma || oma.motion) return;

  const send = (message) => {
    try { window.omastratorSend(JSON.stringify(message)); } catch (e) { /* not bound yet */ }
  };

  // Starts within two frames of each other are one start.
  const settle = 34;
  // A page with more animations than this is listed in part.
  const cap = 400;

  let held = false;
  // The timeline's zero: the earliest start among the animations held.
  let base = 0;
  // The last time asked for, so an animation that starts later joins at the playhead.
  let lastSeek = 0;
  let serial = 0;
  let lastKey = "";
  let scheduled = false;
  // Counts up on each hold and release, so a check queued by an earlier hold ends instead of running beside the new one.
  let chain = 0;
  // Animation -> { offset, finished }: where its own time zero sits on the timeline, and whether it had ended.
  const records = new Map();
  // Element -> state, for the pseudo-classes Omastrator forces.
  let forced = new Map();
  // Elements whose reverse transition, after a forced state was let go, is not motion of the page: element -> { until, looked },
  // kept for a second and until one look has been made, since a hidden page may not look for longer than that.
  const released = new Map();
  let gsapWasRunning = false;

  const scrollKind = (anim) => {
    if (typeof ViewTimeline !== "undefined" && anim.timeline instanceof ViewTimeline) return "view";
    if (typeof ScrollTimeline !== "undefined" && anim.timeline instanceof ScrollTimeline) return "scroll";
    return "";
  };

  const mine = (anim) => {
    const effect = anim.effect;
    const target = effect && effect.target;
    return !!target && typeof effect.getTiming === "function" && target.getRootNode() === document && !target.closest("#omastrator-overlay");
  };

  const animations = () => document.getAnimations().filter(mine);

  const kebab = (name) => (name === "cssFloat" ? "float" : name.replace(/[A-Z]/g, (c) => "-" + c.toLowerCase()));
  const notProperty = new Set(["offset", "easing", "composite", "computedOffset"]);

  function shortOf(element) {
    const first = (element.getAttribute("class") || "").split(/\s+/).filter(Boolean)[0] || "";
    return { tag: element.tagName.toLowerCase(), id: element.id || "", cls: first };
  }

  function keyframesOf(effect) {
    const frames = [];
    for (const frame of effect.getKeyframes()) {
      const props = {};
      for (const key of Object.keys(frame)) if (!notProperty.has(key)) props[kebab(key)] = String(frame[key]);
      frames.push({ offset: frame.computedOffset, easing: frame.easing, composite: frame.composite, props });
    }
    return frames;
  }

  function timingOf(effect) {
    const t = effect.getTiming();
    const finite = (v) => (typeof v === "number" && isFinite(v) ? v : 0);
    return { delay: finite(t.delay), endDelay: finite(t.endDelay), duration: finite(t.duration),
      iterations: t.iterations === Infinity ? -1 : t.iterations, fill: t.fill, direction: t.direction, easing: t.easing };
  }

  // ------------------------------------------------------------ scroll-driven motion

  // Where a range name sits on the scroll axis, as the CSS scroll-driven animations specification names them.
  function named(name, cover, length, size) {
    const inside = Math.min(length, size);
    let range = cover;
    if (name === "entry") range = [cover[0], cover[0] + inside];
    else if (name === "exit") range = [cover[1] - inside, cover[1]];
    else if (name === "contain") range = [cover[0] + inside, cover[1] - inside];
    else if (name === "entry-crossing") range = [cover[0], cover[0] + length];
    else if (name === "exit-crossing") range = [cover[1] - length, cover[1]];
    return [Math.min(range[0], range[1]), Math.max(range[0], range[1])];
  }

  // A range bound (`animation-range-start` or `-end`) as a scroll offset in px.
  function bound(value, natural, cover, length, size, fallback) {
    if (!value) return fallback;
    if (value.rangeName) {
      const range = named(value.rangeName, cover, length, size);
      const offset = value.offset;
      const percent = offset && offset.unit === "percent" ? offset.value : 0;
      const px = offset && offset.unit === "px" ? offset.value : 0;
      return range[0] + (range[1] - range[0]) * percent / 100 + px;
    }
    if (value.unit === "percent") return natural[0] + (natural[1] - natural[0]) * value.value / 100;
    if (value.unit === "px") return natural[0] + value.value;
    return fallback;
  }

  // The scroll positions, in px, between which the animation runs.
  function rangeOf(anim, kind) {
    const timeline = anim.timeline;
    const axis = timeline.axis || "block";
    const vertical = axis === "block" || axis === "y";
    const source = timeline.source || document.scrollingElement;
    const root = source === document.scrollingElement || source === document.documentElement || source === document.body;
    const size = vertical ? (root ? innerHeight : source.clientHeight) : (root ? innerWidth : source.clientWidth);
    const total = vertical ? source.scrollHeight : source.scrollWidth;
    const max = Math.max(0, total - size);
    let natural = [0, max];
    let cover = natural;
    let length = 0;
    if (kind === "view" && timeline.subject) {
      const rect = timeline.subject.getBoundingClientRect();
      const edge = root ? { top: 0, left: 0 } : source.getBoundingClientRect();
      const at = vertical ? rect.top - edge.top + (root ? scrollY : source.scrollTop) : rect.left - edge.left + (root ? scrollX : source.scrollLeft);
      length = vertical ? rect.height : rect.width;
      cover = [at - size, at + length];
      natural = cover;
    }
    const from = bound(anim.rangeStart, natural, cover, length, size, natural[0]);
    const to = bound(anim.rangeEnd, natural, cover, length, size, natural[1]);
    return { from: Math.min(from, to), to: Math.max(from, to), axis: vertical ? "y" : "x", max };
  }

  // ------------------------------------------------------------ states that need a pointer

  const stateEnd = /:(hover|focus-visible|focus-within|focus|active)$/;
  let ruleKey = "";
  let ruleCache = [];

  // The rules that name a pointer or focus state on their last compound, with the properties they set.
  function stateRules() {
    let count = document.styleSheets.length;
    for (const sheet of document.styleSheets) {
      try { count += sheet.cssRules.length; } catch (e) { /* a cross-origin sheet */ }
    }
    if (String(count) === ruleKey) return ruleCache;
    const found = [];
    const visit = (rules) => {
      for (const rule of rules) {
        if (typeof CSSStyleRule !== "undefined" && rule instanceof CSSStyleRule) {
          for (const part of rule.selectorText.split(",")) {
            const text = part.trim();
            const match = stateEnd.exec(text);
            if (!match) continue;
            const before = text.slice(0, match.index);
            if (!before || /[\s>+~]$/.test(before)) continue;
            const properties = [];
            for (let i = 0; i < rule.style.length; i++) properties.push(rule.style[i]);
            found.push({ base: before, state: match[1], properties });
          }
        }
        if (rule.cssRules && rule.cssRules.length) visit(rule.cssRules);
      }
    };
    for (const sheet of document.styleSheets) {
      try { visit(sheet.cssRules); } catch (e) { /* a cross-origin sheet */ }
    }
    ruleKey = String(count);
    ruleCache = found;
    return found;
  }

  // A value list split at its top-level commas: "cubic-bezier(0.1, 1, 0.3, 1), ease" is two.
  function split(text) {
    const parts = [];
    let depth = 0;
    let start = 0;
    for (let i = 0; i < text.length; i++) {
      if (text[i] === "(") depth++;
      else if (text[i] === ")") depth--;
      else if (text[i] === "," && depth === 0) { parts.push(text.slice(start, i).trim()); start = i + 1; }
    }
    parts.push(text.slice(start).trim());
    return parts.filter(Boolean);
  }

  const seconds = (text) => (text.endsWith("ms") ? parseFloat(text) : parseFloat(text) * 1000) || 0;

  // Transitions the page would run for a state, before any pointer is there to start them.
  function candidates(running) {
    const out = [];
    for (const rule of stateRules()) {
      let elements = [];
      try { elements = Array.from(document.querySelectorAll(rule.base)).slice(0, 6); } catch (e) { continue; }
      for (const element of elements) {
        if (running.has(element) || element.closest("#omastrator-overlay")) continue;
        const style = getComputedStyle(element);
        const listed = split(style.transitionProperty);
        const durations = split(style.transitionDuration).map(seconds);
        const delays = split(style.transitionDelay).map(seconds);
        const easings = split(style.transitionTimingFunction);
        const properties = [];
        let duration = 0;
        let delay = Infinity;
        let easing = "";
        listed.forEach((name, i) => {
          const length = durations[i % durations.length] || 0;
          const wait = delays[i % delays.length] || 0;
          const matches = name === "all" ? rule.properties : rule.properties.filter((p) => p === name || p.startsWith(name + "-"));
          if (length + wait <= 0 || !matches.length) return;
          for (const p of matches) if (!properties.includes(p)) properties.push(p);
          duration = Math.max(duration, length);
          delay = Math.min(delay, wait);
          easing = easing || easings[i % easings.length] || "ease";
        });
        if (!properties.length) continue;
        const parent = element.parentElement;
        out.push({ id: "state:" + rule.state + ":" + oma.selectorFor(element), kind: "css-transition", name: properties.join(", "),
          timeline: "document", selector: oma.selectorFor(element), ...shortOf(element),
          parent: parent ? { selector: oma.selectorFor(parent), ...shortOf(parent) } : null,
          properties, keyframes: [], easing, delay: delay === Infinity ? 0 : delay, endDelay: 0, duration, iterations: 1,
          fill: "none", direction: "normal", offset: 0, state: "idle", trigger: rule.state, potential: true });
      }
    }
    return out;
  }

  // ------------------------------------------------------------ what is listed

  function describe(anim) {
    const effect = anim.effect;
    const target = effect.target;
    const kind = typeof CSSAnimation !== "undefined" && anim instanceof CSSAnimation ? "css-animation"
      : typeof CSSTransition !== "undefined" && anim instanceof CSSTransition ? "css-transition" : "script";
    const keyframes = keyframesOf(effect);
    const properties = [];
    for (const frame of keyframes) for (const name of Object.keys(frame.props)) if (!properties.includes(name)) properties.push(name);
    const timing = timingOf(effect);
    // A CSS animation's easing is set between its keyframes; the effect's own stays linear.
    const easing = timing.easing === "linear" && keyframes.length && keyframes[0].easing ? keyframes[0].easing : timing.easing;
    const name = kind === "css-animation" ? anim.animationName : kind === "css-transition" ? anim.transitionProperty : (anim.id || "");
    const parent = target.parentElement;
    const selector = oma.selectorFor(target) + (effect.pseudoElement || "");
    const timeline = scrollKind(anim);
    const record = records.get(anim);
    const entry = { id: kind + ":" + name + ":" + selector, kind, name, timeline: timeline || "document", selector,
      ...shortOf(target), parent: parent ? { selector: oma.selectorFor(parent), ...shortOf(parent) } : null,
      properties, keyframes, ...timing, easing, state: anim.playState, pseudo: effect.pseudoElement || "" };
    if (timeline) {
      entry.range = rangeOf(anim, timeline);
      entry.trigger = "scroll";
    } else {
      entry.offset = record ? record.offset : 0;
      entry.trigger = kind === "css-animation" ? "load" : kind === "css-transition" ? (forced.get(target) || "change") : "script";
    }
    return entry;
  }

  // GSAP drives its own clock, so its animations are one bar on the global timeline.
  function gsapInfo() {
    const g = window.gsap;
    if (!g || !g.globalTimeline || typeof g.globalTimeline.getChildren !== "function") return null;
    let start = Infinity;
    let end = 0;
    let count = 0;
    try {
      for (const child of g.globalTimeline.getChildren(false, true, true)) {
        const from = child.startTime() * 1000;
        const length = child.totalDuration() * 1000 / (child.timeScale() || 1);
        if (!isFinite(from) || !isFinite(length)) continue;
        start = Math.min(start, from);
        end = Math.max(end, from + length);
        count++;
      }
    } catch (e) { /* a version whose timelines read differently */ }
    return { count, start: count ? start : 0, end: count ? end : 0 };
  }

  // Whether any style sheet has a rule for people who asked for less motion. A page with motion and none is playing it for them.
  function hasReducedRule() {
    let found = false;
    const visit = (rules) => {
      for (const rule of rules) {
        if (found) return;
        if (typeof CSSMediaRule !== "undefined" && rule instanceof CSSMediaRule && /prefers-reduced-motion/.test(rule.media.mediaText)) { found = true; return; }
        if (rule.cssRules && rule.cssRules.length) visit(rule.cssRules);
      }
    };
    for (const sheet of document.styleSheets) {
      try { visit(sheet.cssRules); } catch (e) { /* a cross-origin sheet */ }
      if (found) break;
    }
    return found;
  }

  function list() {
    const all = animations();
    const out = [];
    const running = new Set();
    for (const anim of all) {
      if (anim.effect.target && typeof CSSTransition !== "undefined" && anim instanceof CSSTransition) running.add(anim.effect.target);
      if (out.length < cap) out.push(describe(anim));
    }
    return { held, time: lastSeek, url: location.href, truncated: all.length > cap, reducedRule: hasReducedRule(),
      reduced: matchMedia("(prefers-reduced-motion: reduce)").matches,
      scroll: { y: scrollY, max: Math.max(0, document.documentElement.scrollHeight - innerHeight), viewport: innerHeight },
      animations: out.concat(candidates(running)), gsap: gsapInfo() };
  }

  // ------------------------------------------------------------ holding

  function adopt(anim, late) {
    if (scrollKind(anim)) return;
    const start = typeof anim.startTime === "number" ? anim.startTime : null;
    let offset = !late && start !== null ? start - base : 0;
    if (Math.abs(offset) < settle) offset = 0;
    let finished = false;
    try {
      const end = anim.effect.getComputedTiming().endTime;
      finished = anim.playState === "finished" || (isFinite(end) && typeof anim.currentTime === "number" && anim.currentTime >= end);
    } catch (e) { /* not a keyframe effect */ }
    records.set(anim, { offset, finished });
    try {
      anim.pause();
      if (late) {
        // At the playhead, but never past its end: a transition that has ended is gone, and could not be scrubbed back.
        const end = anim.effect.getComputedTiming().endTime;
        const at = lastSeek - offset;
        anim.currentTime = Math.max(0, isFinite(end) ? Math.min(at, Math.max(0, end - 1)) : at);
      }
    } catch (e) { /* an animation that ended meanwhile */ }
    serial++;
  }

  // Animations the page started while held join the hold; what a state let go of does on its way back is dropped.
  function adoptNew() {
    let joined = false;
    for (const entry of released.values()) entry.looked = true;
    for (const anim of animations()) {
      if (records.has(anim) || scrollKind(anim)) continue;
      if (typeof CSSTransition !== "undefined" && anim instanceof CSSTransition && released.has(anim.effect.target)) { anim.cancel(); continue; }
      adopt(anim, true);
      joined = true;
    }
    return joined;
  }

  // Once per animation frame while held: new animations join, ended ones go, and a change is reported.
  function watch() {
    if (scheduled) return;
    scheduled = true;
    const mine = chain;
    let done = false;
    // The frame and the timer both ask; whichever comes first does the work, and the other finds it done.
    const run = () => {
      if (done || mine !== chain) return;
      done = true;
      scheduled = false;
      if (!held) return;
      const now = performance.now();
      for (const [element, entry] of released) if (entry.until < now && entry.looked) released.delete(element);
      for (const [anim] of records) {
        const target = anim.effect && anim.effect.target;
        if (anim.playState === "idle" || !target || !target.isConnected) { records.delete(anim); serial++; }
      }
      adoptNew();
      const key = animations().length + "/" + serial + "/" + forced.size;
      if (key !== lastKey) {
        lastKey = key;
        send(Object.assign({ type: "motion" }, list()));
      }
      watch();
    };
    requestAnimationFrame(run);
    setTimeout(run, 250);
  }

  // A transition or animation is announced in the frame after it starts, before it has moved: held there, it joins at zero.
  const started = () => { if (held) adoptNew(); };
  addEventListener("transitionrun", started, true);
  addEventListener("animationstart", started, true);

  // ------------------------------------------------------------ edits

  // The inline style each element had before a scrub began, so the edit that follows records the page's own "before".
  const scrubbed = new Map();

  function elementOf(selector) {
    try { return document.querySelector(selector); } catch (e) { return null; }
  }

  const camel = (name) => name.replace(/-([a-z])/g, (m, c) => c.toUpperCase());
  // "from", "to" and "40%" as an offset between 0 and 1.
  function offsetOf(frame) {
    const text = String(frame).trim().toLowerCase();
    if (text === "from") return 0;
    if (text === "to") return 1;
    const value = parseFloat(text);
    return text.endsWith("%") && isFinite(value) ? value / 100 : NaN;
  }

  // The animations named `name` (a CSS animation's @keyframes name, or a script animation's id), on the page.
  const withName = (name) => animations().filter((a) => (a.animationName || a.id) === name && a.effect && a.effect.getKeyframes);

  function keyframeState(name) {
    return { type: "keyframes", items: withName(name).map((a) => ({ selector: oma.selectorFor(a.effect.target), name, keyframes: a.effect.getKeyframes() })) };
  }

  function timingState(name) {
    return { type: "timing", items: withName(name).map((a) => {
      const t = a.effect.getTiming();
      return { selector: oma.selectorFor(a.effect.target), name, timing: { duration: t.duration, delay: t.delay, easing: t.easing } };
    }) };
  }

  // Where each animation of that name runs: on the document's clock, or on a view timeline (scrolling), and its timing.
  function startsState(name) {
    return { type: "starts", items: withName(name).map((a) => {
      const t = a.effect.getTiming();
      return { selector: oma.selectorFor(a.effect.target), name, view: !!scrollKind(a), duration: t.duration, delay: t.delay };
    }) };
  }

  // Puts one animation on the document's clock or on a view timeline of its element, keeping the hold's bookkeeping right.
  const startsMemo = new WeakMap();
  function moveTo(anim, view, timing) {
    const target = anim.effect.target;
    if (view) {
      const t = anim.effect.getTiming();
      if (typeof t.duration === "number") startsMemo.set(anim, { duration: t.duration, delay: t.delay });
      records.delete(anim);
      anim.effect.updateTiming({ duration: "auto", delay: 0 });
      anim.timeline = new ViewTimeline({ subject: target });
      anim.play();
    } else {
      anim.timeline = document.timeline;
      const saved = typeof timing.duration === "number" ? timing : (startsMemo.get(anim) || { duration: 1000, delay: 0 });
      anim.effect.updateTiming({ duration: saved.duration, delay: saved.delay });
      if (held) adopt(anim, true);
    }
    serial++;
  }

  const findAnimation = (item) => withName(item.name).find((a) => oma.selectorFor(a.effect.target) === item.selector);

  const gsapTimeline = () => (window.gsap && window.gsap.globalTimeline && typeof window.gsap.globalTimeline.time === "function" ? window.gsap.globalTimeline : null);

  oma.motion = {
    list,
    // Pauses everything the page is animating and keeps pausing what starts later. Scroll-driven motion is not paused:
    // scrolling is what moves it.
    hold() {
      if (held) return list();
      held = true;
      chain++;
      scheduled = false;
      const all = animations();
      const starts = all.filter((a) => !scrollKind(a) && typeof a.startTime === "number").map((a) => a.startTime);
      base = starts.length ? Math.min(...starts) : 0;
      for (const anim of all) adopt(anim, false);
      let reached = 0;
      for (const [anim, record] of records) {
        if (typeof anim.currentTime === "number") reached = Math.max(reached, anim.currentTime + record.offset);
      }
      lastSeek = reached;
      const g = gsapTimeline();
      if (g) {
        gsapWasRunning = typeof g.paused === "function" ? !g.paused() : true;
        try { g.pause(); lastSeek = Math.max(lastSeek, g.time() * 1000); } catch (e) { /* ignore */ }
      }
      lastKey = "";
      watch();
      return list();
    },
    // Lets the page play on from where it is. What had ended stays ended.
    release() {
      if (!held) return true;
      held = false;
      chain++;
      scheduled = false;
      for (const [anim, record] of records) {
        try {
          const end = anim.effect.getComputedTiming().endTime;
          if (record.finished || (isFinite(end) && anim.currentTime >= end)) anim.finish(); else anim.play();
        } catch (e) { /* ended meanwhile */ }
      }
      records.clear();
      const g = gsapTimeline();
      if (g && gsapWasRunning) { try { g.resume(); } catch (e) { /* ignore */ } }
      forced = new Map();
      released.clear();
      return true;
    },
    // Puts every held animation at `ms` on the timeline, each measured from its own start.
    seek(ms) {
      lastSeek = ms;
      for (const [anim, record] of records) {
        try { anim.currentTime = ms - record.offset; } catch (e) { /* ended meanwhile */ }
      }
      const g = gsapTimeline();
      if (g && held) { try { g.time(Math.max(0, ms) / 1000, false); } catch (e) { /* ignore */ } }
      return ms;
    },
    // Scroll-driven motion is time on the scroll axis: this scrolls the page there.
    seekScroll(px) {
      scrollTo({ left: scrollX, top: px, behavior: "instant" });
      return scrollY;
    },
    // The elements whose pointer or focus state Omastrator holds: [{selector, state}]. One let go is watched for the
    // transition back, which is dropped.
    setForced(list) {
      const next = new Map();
      for (const item of list || []) {
        let element = null;
        try { element = document.querySelector(item.selector); } catch (e) { /* a selector that no longer parses */ }
        if (element) next.set(element, item.state);
      }
      for (const element of forced.keys()) if (!next.has(element)) released.set(element, { until: performance.now() + 1000, looked: false });
      forced = next;
      lastKey = "";
      watch();
      return next.size;
    },
    isHeld: () => held,

    // A scrub step: the value is shown on the element without being recorded; endPreview takes it off before the edit that
    // records it, so its "before" is the page's.
    previewProperty(selector, property, value) {
      const element = elementOf(selector);
      if (!element) return false;
      if (!scrubbed.has(element)) scrubbed.set(element, element.getAttribute("style"));
      element.style.setProperty(property, value);
      return true;
    },
    endPreview() {
      for (const [element, style] of scrubbed) {
        if (style === null) element.removeAttribute("style"); else element.setAttribute("style", style);
      }
      scrubbed.clear();
    },
    // A custom property on an element (motion tokens on :root, --i and --delay-extra on an element): what it was, and the
    // inline style and classes from before and after, for undo.
    setProperty(selector, property, value) {
      const element = elementOf(selector);
      if (!element) return null;
      oma.motion.endPreview();
      const before = getComputedStyle(element).getPropertyValue(property).trim();
      const styleBefore = element.getAttribute("style") || "";
      element.style.setProperty(property, value);
      return { before, styleBefore, styleAfter: element.getAttribute("style") || "", classes: element.getAttribute("class") || "" };
    },
    // One keyframe's value in every animation of that name: setKeyframes on the running animations (a preview of the
    // @keyframes block, which write-back changes). Returns the states that put it back and do it again.
    setKeyframe(name, frame, property, value) {
      const offset = offsetOf(frame);
      if (isNaN(offset)) return null;
      const before = keyframeState(name);
      if (!before.items.length) return null;
      let previous = "";
      for (const item of before.items) {
        const target = findAnimation(item);
        if (!target) continue;
        const frames = item.keyframes.map((k) => Object.assign({}, k));
        const at = frames.find((k) => Math.abs(k.computedOffset - offset) < 0.0005);
        if (!at) return null;
        if (!previous) previous = String(at[camel(property)]);
        at[camel(property)] = value;
        target.effect.setKeyframes(frames);
      }
      return { before: previous, was: before, now: keyframeState(name) };
    },
    // Duration, delay or easing of every animation of that name, for motion the page's code doesn't hold in tokens.
    setTiming(name, changes) {
      const before = timingState(name);
      if (!before.items.length) return null;
      for (const item of before.items) {
        const target = findAnimation(item);
        if (target) target.effect.updateTiming(changes);
      }
      return { was: before, now: timingState(name) };
    },
    // What starts a row's motion, as far as the page can show it: "scroll" moves its animations onto a view timeline of each
    // element, "load" puts them back on the document's clock (they play from the start). Returns the states that undo and redo it,
    // or null where the page can't (no view timelines).
    setStarts(name, to) {
      if (to === "scroll" && typeof ViewTimeline === "undefined") return null;
      const was = startsState(name);
      if (!was.items.length) return null;
      try {
        for (const item of was.items) {
          const anim = findAnimation(item);
          if (!anim) continue;
          if (to === "scroll" && !item.view) moveTo(anim, true, item);
          else if (to === "load" && item.view) moveTo(anim, false, item);
        }
      } catch (e) { return null; }
      return { was, now: startsState(name) };
    },
    // Puts a state from setKeyframe, setTiming or setStarts back.
    undo(state) {
      let done = false;
      for (const item of state.items || []) {
        const target = findAnimation(item);
        if (!target) continue;
        try {
          if (state.type === "keyframes") target.effect.setKeyframes(item.keyframes);
          else if (state.type === "starts") { if (item.view !== !!scrollKind(target)) moveTo(target, item.view, item); }
          else target.effect.updateTiming(item.timing);
          done = true;
        } catch (e) { /* an animation that ended meanwhile */ }
      }
      return done;
    }
  };
})();
