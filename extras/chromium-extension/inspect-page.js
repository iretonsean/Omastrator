// What Inspect reads from a page at a point (docs/ANYWHERE.md). One function, used twice:
// Omastrator compiles it in and evaluates it over DevTools on the Live tab, and the
// extension's worker runs it with chrome.scripting in the tab a window shows.
// (wx, wy) is the point in the window, and (width, height) the window's size.
function omastratorInspect(wx, wy, width, height) {
  var base = { inner: [innerWidth, innerHeight], scroll: [scrollX, scrollY], url: location.href, title: document.title };
  var side = Math.max(0, Math.floor((width - innerWidth) / 2));
  var top = Math.max(0, Math.floor(height - innerHeight) - side);
  var element = document.elementFromPoint(wx - side, wy - top);
  if (!element) return base;
  var paint = document.createElement('canvas').getContext('2d');
  function hex(value) { paint.fillStyle = '#000'; paint.fillStyle = value; return paint.fillStyle; }
  function selector(node) {
    var text = node.tagName.toLowerCase();
    if (node.id) return text + '#' + node.id;
    var classes = (typeof node.className === 'string' ? node.className : '').trim().split(/\s+/).filter(Boolean).slice(0, 2);
    return classes.length ? text + '.' + classes.join('.') : text;
  }
  var style = getComputedStyle(element);
  var background = style.backgroundColor;
  for (var node = element; node && /rgba\(0, 0, 0, 0\)|transparent/.test(background); node = node.parentElement)
    background = getComputedStyle(node).backgroundColor;
  var rect = element.getBoundingClientRect();
  base.element = {
    tag: element.tagName.toLowerCase(), selector: selector(element), rect: [rect.left, rect.top, rect.width, rect.height],
    text: (element.innerText || element.textContent || '').trim().replace(/\s+/g, ' ').slice(0, 80),
    color: hex(style.color), background: hex(background),
    fontFamily: style.fontFamily.split(',')[0].replace(/["']/g, '').trim(), fontSize: parseFloat(style.fontSize), fontWeight: style.fontWeight,
    styles: { lineHeight: style.lineHeight, letterSpacing: style.letterSpacing, padding: style.padding, margin: style.margin,
              borderRadius: style.borderRadius, border: style.border, display: style.display, gap: style.gap,
              width: style.width, height: style.height }
  };
  return base;
}
