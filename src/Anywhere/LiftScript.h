#pragma once

// The page side of Lift (docs/ANYWHERE.md): walks the DOM from an element, or
// every element in a region of the viewport, and answers what each one draws.
// Called as liftPage({mode, rect, maxElements, maxCharacters}); rectangles are
// viewport coordinates in, page coordinates (scrolled) out. Nodes come back
// flattened in paint order, each with its parent's index.
namespace LiftScript {
inline constexpr const char *source = R"js((function (options) {
  var paint = document.createElement('canvas').getContext('2d');
  function hex(value) {
    paint.fillStyle = 'rgba(0, 0, 0, 0)';
    paint.fillStyle = value;
    var c = paint.fillStyle;
    if (c.charAt(0) === '#') return c + 'ff';
    var m = c.match(/[\d.]+/g);
    if (!m) return '#00000000';
    var a = Math.round(parseFloat(m[3] === undefined ? 1 : m[3]) * 255);
    function two(n) { n = Math.max(0, Math.min(255, Math.round(n))); return (n < 16 ? '0' : '') + n.toString(16); }
    return '#' + two(+m[0]) + two(+m[1]) + two(+m[2]) + two(a);
  }
  function transparent(c) { return !c || /00$/.test(c); }
  var sx = scrollX, sy = scrollY;
  var elements = 0, characters = 0, truncated = false;
  var styles = [], styleKeys = {};

  function selectorOf(node) {
    if (node.id && document.querySelectorAll('#' + CSS.escape(node.id)).length === 1) return '#' + CSS.escape(node.id);
    var parts = [];
    for (var at = node; at && at.nodeType === 1 && at !== document.documentElement; at = at.parentElement) {
      if (at.id && document.querySelectorAll('#' + CSS.escape(at.id)).length === 1) { parts.unshift('#' + CSS.escape(at.id)); break; }
      var tag = at.tagName.toLowerCase(), index = 1;
      for (var sib = at.previousElementSibling; sib; sib = sib.previousElementSibling)
        if (sib.tagName === at.tagName) ++index;
      parts.unshift(tag + ':nth-of-type(' + index + ')');
    }
    return parts.join(' > ');
  }
  function nameOf(node) {
    var label = node.getAttribute && node.getAttribute('aria-label');
    var tag = node.tagName.toLowerCase();
    if (label) return tag + ' "' + label.trim().slice(0, 40) + '"';
    if (node.id) return tag + '#' + node.id;
    var classes = (typeof node.className === 'string' ? node.className : '').trim().split(/\s+/).filter(Boolean).slice(0, 2);
    return classes.length ? tag + '.' + classes.join('.') : tag;
  }
  function box(rect) { return [rect.left + sx, rect.top + sy, rect.width, rect.height]; }
  function px(v) { return parseFloat(v) || 0; }
  function radii(cs, w, h) {
    var list = ['borderTopLeftRadius', 'borderTopRightRadius', 'borderBottomRightRadius', 'borderBottomLeftRadius'].map(function (k) {
      var parts = cs[k].split(' ');
      var rx = parts[0].indexOf('%') > 0 ? px(parts[0]) / 100 * w : px(parts[0]);
      var ry = parts.length > 1 ? (parts[1].indexOf('%') > 0 ? px(parts[1]) / 100 * h : px(parts[1])) : (parts[0].indexOf('%') > 0 ? px(parts[0]) / 100 * h : rx);
      return [rx, ry];
    });
    // CSS shrinks every radius alike when two on a side overlap.
    var f = 1;
    function fit(sum, side) { if (sum > side && sum > 0) f = Math.min(f, side / sum); }
    fit(list[0][0] + list[1][0], w); fit(list[3][0] + list[2][0], w); fit(list[0][1] + list[3][1], h); fit(list[1][1] + list[2][1], h);
    return list.map(function (r) { return [r[0] * f, r[1] * f]; });
  }
  function splitTop(text) {
    var out = [], depth = 0, start = 0;
    for (var i = 0; i < text.length; ++i) {
      var ch = text.charAt(i);
      if (ch === '(') ++depth; else if (ch === ')') --depth;
      else if (ch === ',' && depth === 0) { out.push(text.slice(start, i).trim()); start = i + 1; }
    }
    out.push(text.slice(start).trim());
    return out;
  }
  function stops(parts) {
    var list = [];
    parts.forEach(function (part) {
      var m = part.match(/^(.*?\))\s*(.*)$|^(\S+)\s*(.*)$/);
      if (!m) return;
      var color = m[1] || m[3], rest = (m[2] !== undefined ? m[2] : m[4]) || '';
      var positions = rest.split(/\s+/).filter(Boolean).map(function (p) { return p.indexOf('%') > 0 ? px(p) / 100 : null; });
      if (!positions.length) positions = [null];
      positions.forEach(function (p) { list.push({ o: p, c: hex(color) }); });
    });
    if (!list.length) return [];
    if (list[0].o === null) list[0].o = 0;
    if (list[list.length - 1].o === null) list[list.length - 1].o = 1;
    for (var i = 1; i < list.length; ++i) {
      if (list[i].o !== null) { list[i].o = Math.max(list[i].o, list[i - 1].o); continue; }
      var j = i; while (list[j].o === null) ++j;
      for (var k = i; k < j; ++k) list[k].o = list[i - 1].o + (list[j].o - list[i - 1].o) * (k - i + 1) / (j - i + 1);
    }
    return list.map(function (s) { return [s.o, s.c]; });
  }
  function gradient(text) {
    var m = text.match(/^(repeating-)?(linear|radial)-gradient\((.*)\)$/);
    if (!m) return null;
    var parts = splitTop(m[3]);
    if (m[2] === 'linear') {
      var angle = 180, first = parts[0];
      var turns = { 'to top': 0, 'to right': 90, 'to bottom': 180, 'to left': 270, 'to top right': 45, 'to right top': 45,
                    'to bottom right': 135, 'to right bottom': 135, 'to bottom left': 225, 'to left bottom': 225, 'to top left': 315, 'to left top': 315 };
      if (/^-?[\d.]+(deg|turn|rad|grad)$/.test(first)) {
        var v = parseFloat(first);
        angle = /turn$/.test(first) ? v * 360 : /grad$/.test(first) ? v * 0.9 : /rad$/.test(first) ? v * 180 / Math.PI : v;
        parts.shift();
      } else if (turns[first] !== undefined) {
        angle = turns[first];
        parts.shift();
      }
      return { kind: 'linear', angle: angle, stops: stops(parts) };
    }
    var shape = { kind: 'radial', cx: 0.5, cy: 0.5, circle: false, stops: [] };
    if (!/^(#|rgb|hsl|oklch|oklab|lab|lch|color|[a-z]+$)/.test(parts[0]) || /\bat\b|circle|ellipse|closest|farthest/.test(parts[0])) {
      var head = parts.shift();
      shape.circle = /circle/.test(head);
      var at = head.split(/\bat\b/)[1];
      if (at) {
        var words = at.trim().split(/\s+/);
        var named = { left: 0, top: 0, center: 0.5, right: 1, bottom: 1 };
        function pos(w) { return named[w] !== undefined ? named[w] : w.indexOf('%') > 0 ? px(w) / 100 : null; }
        if (words[0]) shape.cx = pos(words[0]) === null ? 0.5 : pos(words[0]);
        if (words[1]) shape.cy = pos(words[1]) === null ? 0.5 : pos(words[1]);
        if (words[0] === 'top' || words[0] === 'bottom') { shape.cy = pos(words[0]); shape.cx = words[1] ? pos(words[1]) : 0.5; }
      }
    }
    shape.stops = stops(parts);
    return shape;
  }
  function images(cs) {
    var list = [];
    if (cs.backgroundImage === 'none') return list;
    var layers = splitTop(cs.backgroundImage), sizes = splitTop(cs.backgroundSize), positions = splitTop(cs.backgroundPosition);
    layers.forEach(function (layer, index) {
      var url = layer.match(/^url\(["']?(.*?)["']?\)$/);
      if (url) list.push({ kind: 'image', url: url[1], size: sizes[index % sizes.length], position: positions[index % positions.length] });
      else { var g = gradient(layer); if (g) list.push(g); }
    });
    // CSS draws the first layer on top.
    return list.reverse();
  }
  function shadows(text) {
    if (!text || text === 'none') return [];
    return splitTop(text).map(function (one) {
      var color = (one.match(/(rgba?\([^)]*\)|#[0-9a-f]+)/i) || ['rgba(0,0,0,0.5)'])[0];
      var rest = one.replace(color, ' ');
      var n = (rest.match(/-?[\d.]+px|-?[\d.]+(?=\s|$)/g) || []).map(px);
      return { x: n[0] || 0, y: n[1] || 0, blur: n[2] || 0, spread: n[3] || 0, c: hex(color), inset: /inset/.test(rest) };
    });
  }
  function styleIndex(cs, extra) {
    var tt = cs.textTransform;
    var s = { f: cs.fontFamily, sz: px(cs.fontSize), w: parseInt(cs.fontWeight, 10) || 400, it: cs.fontStyle !== 'normal',
              c: hex(cs.color), ls: cs.letterSpacing === 'normal' ? 0 : px(cs.letterSpacing), up: tt === 'uppercase',
              u: /underline/.test(cs.textDecorationLine), st: /line-through/.test(cs.textDecorationLine),
              lh: cs.lineHeight === 'normal' ? 0 : px(cs.lineHeight) };
    if (extra) for (var k in extra) s[k] = extra[k];
    var key = JSON.stringify(s);
    if (styleKeys[key] === undefined) { styleKeys[key] = styles.length; styles.push(s); }
    return styleKeys[key];
  }
  function isInline(node, cs) {
    if (node.nodeType !== 1) return false;
    var tag = node.tagName;
    if (/^(IMG|SVG|svg|CANVAS|VIDEO|IFRAME|INPUT|TEXTAREA|SELECT|BUTTON|OBJECT|EMBED|PICTURE)$/.test(tag)) return false;
    return cs.display === 'inline' && cs.position !== 'absolute' && cs.position !== 'fixed' && cs.transform === 'none';
  }
  // The element's words, line by line as the browser broke them; inline children become runs.
  function textOf(element, extraBoxes) {
    var tokens = [];
    var pendingSpace = false;
    function visit(node, cs) {
      for (var child = node.firstChild; child; child = child.nextSibling) {
        if (child.nodeType === 3) {
          var raw = child.nodeValue;
          if (!raw || characters > options.maxCharacters) continue;
          var keep = /^(pre|pre-wrap|break-spaces)$/.test(cs.whiteSpace);
          var style = styleIndex(cs);
          var re = /\S+/g, m;
          if (cs.textTransform === 'lowercase') raw = raw.toLowerCase();
          while ((m = re.exec(raw))) {
            var range = document.createRange();
            range.setStart(child, m.index);
            range.setEnd(child, m.index + m[0].length);
            var rects = range.getClientRects();
            if (!rects.length) continue;
            var word = m[0];
            if (cs.textTransform === 'capitalize') word = word.charAt(0).toUpperCase() + word.slice(1);
            var gap = m.index > 0 ? /\s/.test(raw.charAt(m.index - 1)) : pendingSpace;
            pendingSpace = false;
            var before = keep ? (raw.slice(0, m.index).match(/[ \t]*$/) || [''])[0] : (gap ? ' ' : '');
            tokens.push({ t: word, s: style, r: rects[0], space: before, last: rects[rects.length - 1] });
            characters += word.length;
          }
          // A space at the end of one text node separates it from the next one's first word.
          if (/\s$/.test(raw)) pendingSpace = true;
          else if (/\S/.test(raw)) pendingSpace = false;
        } else if (child.nodeType === 1) {
          var ccs = getComputedStyle(child);
          if (ccs.display === 'none' || ccs.visibility === 'hidden') continue;
          if (child.tagName === 'BR') { tokens.push({ br: true }); continue; }
          if (!isInline(child, ccs)) continue;
          var bg = hex(ccs.backgroundColor);
          if (!transparent(bg)) {
            var list = child.getClientRects();
            for (var i = 0; i < list.length; ++i) extraBoxes.push({ r: box(list[i]), c: bg, rad: px(ccs.borderTopLeftRadius) });
          }
          visit(child, ccs);
        }
      }
    }
    visit(element, getComputedStyle(element));
    if (!tokens.length) return null;
    var lines = [], line = null;
    tokens.forEach(function (token) {
      if (token.br) { line = null; return; }
      var top = token.r.top, height = token.r.height;
      if (!line || top > line.top + line.height * 0.6 || token.r.left < line.right - 1) {
        line = { top: top, height: height, left: token.r.left, right: token.r.right, runs: [], y: top + sy, h: height };
        lines.push(line);
        token.space = '';
      }
      line.right = Math.max(line.right, token.last.right);
      var text = token.space + token.t;
      var run = line.runs[line.runs.length - 1];
      if (run && run.s === token.s) run.t += text;
      else if (run && token.space) { run.t += token.space; line.runs.push({ t: token.t, s: token.s, r: [token.r.left + sx, token.r.top + sy, token.r.width, token.r.height] }); }
      else line.runs.push({ t: text, s: token.s, r: [token.r.left + sx, token.r.top + sy, token.r.width, token.r.height] });
    });
    return {
      align: getComputedStyle(element).textAlign,
      lines: lines.map(function (l) { return { x: l.left + sx, y: l.y, w: l.right - l.left, h: l.h, runs: l.runs }; })
    };
  }
  function svgMarkup(svg) {
    var clone = svg.cloneNode(true);
    var from = [svg].concat(Array.prototype.slice.call(svg.querySelectorAll('*')));
    var to = [clone].concat(Array.prototype.slice.call(clone.querySelectorAll('*')));
    var props = ['fill', 'stroke', 'stroke-width', 'opacity', 'fill-opacity', 'stroke-opacity', 'fill-rule', 'stroke-linecap', 'stroke-linejoin',
                 'font-family', 'font-size', 'font-weight'];
    for (var i = 0; i < from.length && i < to.length; ++i) {
      var cs = getComputedStyle(from[i]);
      props.forEach(function (p) {
        var v = cs.getPropertyValue(p);
        if (!v) return;
        if (p === 'fill' || p === 'stroke') v = v === 'none' ? 'none' : /^url/.test(v) ? v : hex(v).slice(0, 7);
        to[i].setAttribute(p, v);
      });
    }
    // Sprites: <use> becomes a copy of what it points at, which the importer can read.
    Array.prototype.slice.call(clone.querySelectorAll('use')).forEach(function (use) {
      var ref = use.getAttribute('href') || use.getAttribute('xlink:href') || '';
      var target = ref.charAt(0) === '#' ? document.getElementById(ref.slice(1)) : null;
      if (!target) return;
      var g = document.createElementNS('http://www.w3.org/2000/svg', 'g');
      var x = parseFloat(use.getAttribute('x')) || 0, y = parseFloat(use.getAttribute('y')) || 0;
      g.setAttribute('transform', 'translate(' + x + ' ' + y + ')');
      var copy = target.cloneNode(true);
      if (copy.tagName.toLowerCase() === 'symbol') {
        Array.prototype.slice.call(copy.childNodes).forEach(function (c) { g.appendChild(c); });
        var vb = (target.getAttribute('viewBox') || '').split(/[\s,]+/).map(parseFloat);
        var w = parseFloat(use.getAttribute('width')) || svg.getBoundingClientRect().width, h = parseFloat(use.getAttribute('height')) || svg.getBoundingClientRect().height;
        if (vb.length === 4 && vb[2] > 0 && vb[3] > 0) {
          var s = Math.min(w / vb[2], h / vb[3]);
          g.setAttribute('transform', 'translate(' + x + ' ' + y + ') scale(' + s + ') translate(' + (-vb[0]) + ' ' + (-vb[1]) + ')');
        }
      } else {
        g.appendChild(copy);
      }
      ['fill', 'stroke'].forEach(function (p) { if (use.getAttribute(p)) g.setAttribute(p, use.getAttribute(p)); });
      use.parentNode.replaceChild(g, use);
    });
    var r = svg.getBoundingClientRect();
    clone.setAttribute('width', r.width);
    clone.setAttribute('height', r.height);
    clone.setAttribute('xmlns', 'http://www.w3.org/2000/svg');
    return new XMLSerializer().serializeToString(clone);
  }
  function layerOf(cs) {
    var positioned = cs.position !== 'static';
    var z = parseInt(cs.zIndex, 10);
    if (positioned && z < 0) return -1000000 + z;
    if (!positioned) return cs.float !== 'none' ? 1 : 0;
    return isNaN(z) || z === 0 ? 2 : 3 + z;
  }
  var region = options.rect ? { x: options.rect[0] + sx, y: options.rect[1] + sy, w: options.rect[2], h: options.rect[3] } : null;
  function meets(r) {
    if (!region) return true;
    return r[0] < region.x + region.w && r[0] + r[2] > region.x && r[1] < region.y + region.h && r[1] + r[3] > region.y;
  }

  function walk(element, depth) {
    var cs = getComputedStyle(element);
    if (cs.display === 'none' || depth > 60) return null;
    if (++elements > options.maxElements) { truncated = true; return null; }
    var tag = element.tagName.toLowerCase();
    var node = { sel: selectorOf(element), name: nameOf(element), tag: tag, kids: [] };
    var undo = null;
    if (cs.transform !== 'none') {
      var o = cs.transformOrigin.split(' ').map(px);
      var matrix = new DOMMatrix(cs.transform);
      // Measured without its transform, so the transform can be put back on the group.
      undo = [element.style.getPropertyValue('transform'), element.style.getPropertyPriority('transform'),
              element.style.getPropertyValue('transition'), element.style.getPropertyPriority('transition')];
      element.style.setProperty('transition', 'none', 'important');
      element.style.setProperty('transform', 'none', 'important');
      var plain = element.getBoundingClientRect();
      var ox = plain.left + sx + o[0], oy = plain.top + sy + (o[1] || 0);
      var full = new DOMMatrix().translate(ox, oy).multiply(matrix).translate(-ox, -oy);
      node.tf = [full.a, full.b, full.c, full.d, full.e, full.f];
    }
    var rect = element.getBoundingClientRect();
    node.r = box(rect);
    var visible = cs.visibility !== 'hidden' && rect.width > 0 && rect.height > 0;
    if (px(cs.opacity) < 1) node.op = px(cs.opacity);
    if (visible && meets(node.r)) {
      var rad = radii(cs, rect.width, rect.height);
      var fills = [];
      var bg = hex(cs.backgroundColor);
      if (!transparent(bg)) fills.push({ kind: 'solid', c: bg });
      fills = fills.concat(images(cs));
      var bw = [px(cs.borderTopWidth), px(cs.borderRightWidth), px(cs.borderBottomWidth), px(cs.borderLeftWidth)];
      var styleNames = [cs.borderTopStyle, cs.borderRightStyle, cs.borderBottomStyle, cs.borderLeftStyle];
      bw = bw.map(function (w, i) { return styleNames[i] === 'none' || styleNames[i] === 'hidden' ? 0 : w; });
      var border = null;
      if (bw.some(function (w) { return w > 0; }))
        border = { w: bw, c: [hex(cs.borderTopColor), hex(cs.borderRightColor), hex(cs.borderBottomColor), hex(cs.borderLeftColor)], s: styleNames };
      var sh = shadows(cs.boxShadow);
      if (fills.length || border || sh.length) node.box = { fills: fills, rad: rad, border: border, shadows: sh };
      if (tag === 'img' && element.complete && element.naturalWidth > 0) {
        node.img = { url: element.currentSrc || element.src, fit: cs.objectFit, position: cs.objectPosition,
                     natural: [element.naturalWidth, element.naturalHeight], rad: rad };
      } else if (tag === 'svg') {
        node.svg = svgMarkup(element);
      } else if (/^(canvas|video|iframe|embed|object)$/.test(tag)) {
        node.shot = true;
      } else if (/^(input|textarea|select)$/.test(tag)) {
        var value = tag === 'select' ? (element.options[element.selectedIndex] || {}).text : element.value || element.placeholder || '';
        if (value && element.type !== 'password' && element.type !== 'hidden' && element.type !== 'checkbox' && element.type !== 'radio') {
          var left = rect.left + px(cs.borderLeftWidth) + px(cs.paddingLeft);
          var size = px(cs.fontSize), h = size * 1.2, top = rect.top + (rect.height - h) / 2;
          var color = element.value ? null : hex(getComputedStyle(element, '::placeholder').color);
          var s = styleIndex(cs, color ? { c: color } : null);
          node.text = { align: 'left', lines: [{ x: left + sx, y: top + sy, w: rect.width, h: h, runs: [{ t: value.split('\n')[0], s: s }] }] };
        }
      }
      if (!node.img && !node.svg && !node.shot && !node.text) {
        var inlineBoxes = [];
        var text = textOf(element, inlineBoxes);
        if (text) node.text = text;
        if (inlineBoxes.length) node.marks = inlineBoxes;
      }
      if (cs.overflowX !== 'visible' || cs.overflowY !== 'visible') {
        var bt = px(cs.borderTopWidth), br = px(cs.borderRightWidth), bb = px(cs.borderBottomWidth), bl = px(cs.borderLeftWidth);
        node.clip = { r: [node.r[0] + bl, node.r[1] + bt, node.r[2] - bl - br, node.r[3] - bt - bb],
                      rad: rad.map(function (r, i) {
                        var sideX = i === 0 || i === 3 ? bl : br, sideY = i < 2 ? bt : bb;
                        return [Math.max(0, r[0] - sideX), Math.max(0, r[1] - sideY)];
                      }) };
      }
    }
    {
      if (!node.svg && !node.img) {
        var kids = [];
        for (var child = element.firstElementChild; child; child = child.nextElementSibling) {
          var ccs = getComputedStyle(child);
          if (node.text && isInline(child, ccs)) continue;
          kids.push({ element: child, layer: layerOf(ccs), order: kids.length });
        }
        kids.sort(function (a, b) { return a.layer - b.layer || a.order - b.order; });
        kids.forEach(function (kid) { var made = walk(kid.element, depth + 1); if (made) node.kids.push(made); });
      }
    }
    if (undo) {
      element.style.setProperty('transform', undo[0], undo[1]);
      element.style.setProperty('transition', undo[2], undo[3]);
    }
    var own = node.box || node.text || node.img || node.svg || node.shot || node.marks;
    if (!own && !node.kids.length) return null;
    if (!visible && cs.visibility === 'hidden' && !node.kids.length) return null;
    // A wrapper that draws nothing and holds one thing is that thing.
    if (!own && node.kids.length === 1 && !node.tf && !node.op && !node.clip && depth > 0) return node.kids[0];
    return node;
  }

  var root = null, page = null;
  if (options.mode === 'element') {
    var cx = options.rect[0] + options.rect[2] / 2, cy = options.rect[1] + options.rect[3] / 2;
    var at = document.elementFromPoint(cx, cy);
    // The element the bar showed: the one under its centre whose box it was.
    for (var e = at; e; e = e.parentElement) {
      var r = e.getBoundingClientRect();
      if (Math.abs(r.left - options.rect[0]) <= 1.5 && Math.abs(r.top - options.rect[1]) <= 1.5 &&
          Math.abs(r.width - options.rect[2]) <= 1.5 && Math.abs(r.height - options.rect[3]) <= 1.5) { root = e; break; }
    }
    if (!root) root = at;
    region = null;
  }
  if (!root || root === document.documentElement || root === document.body) {
    root = document.body;
    if (!region) region = { x: sx, y: sy, w: innerWidth, h: innerHeight };
    var canvasColor = hex(getComputedStyle(document.documentElement).backgroundColor);
    if (transparent(canvasColor)) canvasColor = hex(getComputedStyle(document.body).backgroundColor);
    if (transparent(canvasColor)) canvasColor = '#ffffffff';
    page = { r: [region.x, region.y, region.w, region.h], c: canvasColor };
  }
  if (!root) return { error: 'There is nothing on the page to lift.' };
  var tree = walk(root, 0);
  var nodes = [];
  function flatten(node, parent) {
    var index = nodes.length;
    var kids = node.kids;
    delete node.kids;
    node.p = parent;
    node.k = kids.length;
    nodes.push(node);
    kids.forEach(function (kid) { flatten(kid, index); });
  }
  if (tree) flatten(tree, -1);
  return { url: location.href, title: document.title, scroll: [sx, sy], inner: [innerWidth, innerHeight], dpr: devicePixelRatio,
           page: page, region: region ? [region.x, region.y, region.w, region.h] : null,
           nodes: nodes, styles: styles, truncated: truncated, elements: elements };
}))js";
}
