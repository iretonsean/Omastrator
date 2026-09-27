// Optional Vite helper for Omastrator's Live mode (docs/OS-SUITE.md).
// In `vite dev` only, it marks each element of your HTML pages with
// data-oma-src="path:line:column", so Live's write-back knows exactly where an
// element is written even when its text or classes appear elsewhere too.
// Builds are untouched. Nothing is required: without it, Live searches.
//
//   // vite.config.js
//   import omastrator from "./node_modules/vite-plugin-omastrator/index.js";
//   export default { plugins: [omastrator()] };
import { relative } from "node:path";

// Elements that never render, or whose content isn't markup.
const skipped = new Set(["html", "head", "meta", "link", "script", "style", "title", "base", "template", "noscript"]);

export function markSources(html, file) {
  let out = "";
  let last = 0;
  let line = 1;
  let lineStart = 0;
  let counted = 0;
  const tag = /<([a-zA-Z][\w-]*)(?=[\s>/])|<(script|style)\b[\s\S]*?<\/\2>|<!--[\s\S]*?-->/g;
  for (let match; (match = tag.exec(html)); ) {
    for (; counted < match.index; counted++) {
      if (html[counted] === "\n") { line++; lineStart = counted + 1; }
    }
    const name = match[1];
    if (!name || skipped.has(name.toLowerCase())) continue;
    const end = match.index + match[0].length;
    out += html.slice(last, end) + ` data-oma-src="${file}:${line}:${match.index - lineStart + 1}"`;
    last = end;
  }
  return out + html.slice(last);
}

export default function omastrator() {
  let root = process.cwd();
  return {
    name: "omastrator-source",
    apply: "serve",
    configResolved(config) { root = config.root; },
    transformIndexHtml: {
      order: "pre",
      handler(html, context) {
        return markSources(html, relative(root, context.filename || "index.html"));
      }
    }
  };
}
