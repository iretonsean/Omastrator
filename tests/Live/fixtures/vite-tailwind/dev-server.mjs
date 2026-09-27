// Stands in for `vite` so the tests need no network: it serves this folder on
// a free port and prints the banner Vite prints, which Omastrator reads.
import { createServer } from "node:http";
import { readFile } from "node:fs/promises";
import { extname, join, normalize } from "node:path";

const root = new URL(".", import.meta.url).pathname;
const types = { ".html": "text/html", ".css": "text/css", ".js": "text/javascript", ".svg": "image/svg+xml" };

const server = createServer(async (request, response) => {
  const path = normalize(decodeURIComponent(new URL(request.url, "http://x").pathname));
  const file = join(root, path.endsWith("/") ? path + "index.html" : path);
  if (!file.startsWith(root)) { response.writeHead(403).end(); return; }
  try {
    const body = await readFile(file);
    response.writeHead(200, { "content-type": types[extname(file)] || "application/octet-stream" }).end(body);
  } catch {
    response.writeHead(404).end("not found");
  }
});

server.listen(0, "127.0.0.1", () => {
  const { port } = server.address();
  console.log(`\n  VITE v6.0.0  ready in 123 ms\n\n  ➜  Local:   http://localhost:${port}/\n  ➜  Network: use --host to expose\n`);
});
