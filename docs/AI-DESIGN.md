# AI features: design

How the plan in [AI-ROADMAP.md](AI-ROADMAP.md) is built. Omastrator bundles no
model. It launches the user's Omarchy default agent and exposes the open
document to that agent.

## Pieces

```
Omastrator (GUI)                            the agent (claude, codex, opencode, pi…)
┌──────────────────────────────┐            launched by `omarchy agent prompt "<prompt>"`
│ AgentServer (QLocalServer)   │◀─ JSON-RPC ─ `omastrator agent <method> [json]`   (any agent: shell)
│   $XDG_RUNTIME_DIR/          │◀─ JSON-RPC ─ `omastrator --mcp`                    (MCP stdio bridge)
│   omastrator.sock            │
│ AgentTools ─▶ EditorSession  │
│ AgentProposal (preview)      │
│ AgentLauncher ─▶ omarchy     │
└──────────────────────────────┘
```

- **AgentServer.** A `QLocalServer` in the running app, listening on
  `$XDG_RUNTIME_DIR/omastrator.sock` (with `/tmp/omastrator-<uid>.sock` as the
  fallback). The protocol is newline-delimited JSON-RPC 2.0. The server accepts
  connections from the same user only; socket permissions are 0600.
- **CLI client.** `omastrator agent <method> [json-params]` sends one request,
  prints the result as JSON on stdout, and exits 0. On an error it prints the
  message on stderr and exits 1. If no app is running, it says so. This is the
  route any agent can use, because every agent can run a shell command.
- **MCP bridge.** `omastrator --mcp` is a stdio MCP server. It answers
  `initialize`, `tools/list` (every method below with a JSON schema) and
  `tools/call` (forwarded over the socket). No GUI starts, so it runs under
  `QCoreApplication`. Claude Code registers it with
  `claude mcp add omastrator -- omastrator --mcp`, and Help ▸ Connect an Agent…
  shows that line.
- **AgentLauncher.** Runs `omarchy default agent`. If nothing is set, it says:
  "Choose an agent in Omarchy → Setup → Default → Agent". It then runs
  `omarchy agent prompt "<prompt>"` with arguments passed directly (never
  through a shell), and sets `OMASTRATOR_BIN` and `OMASTRATOR_SOCKET` in the
  environment. The working directory is `~/.local/share/omastrator/agent/`,
  which contains `AGENTS.md` and `CLAUDE.md` (how to drive Omastrator with the
  CLI) and `.mcp.json` (the MCP bridge), written fresh on every launch. Each
  prompt names the task and the id of the request it answers.

## Preview, then accept

Every document edit an agent makes goes into a **proposal**. This is one open
`EditorSession` interaction named "AI: <title>". The canvas shows it live, with
a bar above the canvas: *"<title>: Enter keeps it, Esc discards it."* Enter
calls `commitInteraction()`, so the whole proposal becomes one undo step.
Esc calls `cancelInteraction()`. While a proposal is open:

- the canvas tools are paused
- the agent's further edits add to the same proposal
- `proposal_finish` ends the agent's turn but still waits for the user's answer

## Methods

The same names serve the CLI and MCP. Parameters and results are JSON. Ids are
UUID strings.

**Read**
- `document_get {}`: the whole document as `DocumentCodec` JSON, plus
  `selection` and `activeLayer`.
- `selection_get {}`: the selected objects' JSON and their bounds.
- `render {scale?=1, selectionOnly?=false, path?}`: renders a PNG. It is written
  to `path`, or to a temporary file whose path is returned. This lets an agent
  see the artboard.

**Edit** (each goes into the proposal)
- `insert_svg {svg, name?, at?: [x, y], fit?: [w, h]}`: imported with
  `SvgImporter::parse`, grouped, and placed into the active layer. Returns the
  group's id.
- `set_style {ids?, fill?, stroke?, opacity?, blendMode?}`: paint in
  `DocumentCodec` JSON form.
- `transform {ids?, matrix?: [a, b, c, d, e, f], translate?, rotate?, scale?,
  origin?}`
- `arrange {ids?, order}`, `align {ids?, edge, target?}`,
  `distribute {ids?, axis}`
- `group {ids}`, `ungroup {ids}`
- `pathfinder {ids, operation}`
- `delete {ids}`, `select {ids}`
- `update_object {object}`: replaces one object with the given JSON.
- `replace_objects {ids, svg}`: swaps objects for new SVG art, keeping their
  place in the z-order. This is how smart tracing returns cleaned-up art.
- `proposal_finish {title?, summary?}`: the agent is done. The accept bar shows
  the summary.

**Files**
- `open {path}`, `save {path?}`, `export {path, format?, scale?, quality?,
  transparent?}`
- `place {path}`

**Results for the panels** (these are not document edits)
- `show_variations {requestId, variations: [{name, svg, note?}]}`: fills the
  Variations panel.
- `show_roast {requestId, roast, feedback: [{title, detail, objectIds?}],
  suggestedPrompt}`: fills the Roast panel.
- `trace_image {id?, mode: "color" | "blackAndWhite", colors?}`: the classic
  `ImageTrace`, run in the app. It returns the new objects' ids, and the agent
  cleans them up next.

**Session** (not document edits)
- `select_tool {tool}`: chooses the canvas tool, with or without a document.
  Takes the toolbar's names (`select`, `directSelect`, `pen`, …) and a few
  aliases (`move`, `direct`, `type`).
- `status_get {}`: the tool, whether a document is open, the proposal title
  and summary, the agent task waited on (`waiting`, `task`, `agent`), the
  newest variations (`variations`, `variationsId`), `roastId`, `ready`
  (results the user hasn't acted on) and `error`.
- `status_follow {}` is answered on the socket only, not over MCP: it returns
  `status_get`'s answer, then sends a `{"method": "status", "params": …}`
  notification each time that answer changes. `omastrator status --follow`
  uses it; see [OS-SUITE.md](OS-SUITE.md).

## Flows

1. **Generate…** (Object ▸ Generate…). A prompt, a variation count from 1 to 6
   (default 3), and optionally "fit to selection". The agent is launched with
   the brief. It calls `show_variations`, and the Variations panel shows
   thumbnails rendered by `VectorRenderer`. Picking one inserts it as a
   proposal. **Refine** takes an instruction ("rounder", "fewer colours") and
   relaunches with the chosen SVG and the history of the conversation. The
   result is a new set of variations to pick from.
2. **Edit with instruction** (Object ▸ Edit with Instruction…). The instruction
   runs on the selection, or on the document when nothing is selected. The agent
   calls the edit methods, then `proposal_finish`.
3. **Smart trace** (Object ▸ Image Trace ▸ Vectorize with AI…, with the modes
   *Logo & icon* and *Sketch & line art*):
   - The app runs `ImageTrace` locally first, so the user sees something
     immediately.
   - The agent then calls `render` on the original image and uses
     `replace_objects` with cleaner SVG:
     - *Logo & icon:* flat colours, true circles and straight lines, symmetry.
     - *Sketch & line art:* centreline strokes, not filled blobs.
4. **Roast My Design** (the bottom of the tool rail). The agent is sent a render
   of the selection or artboard and calls `show_roast`. The Roast panel shows
   the roast, then the sincere feedback, then **Make variations from this
   feedback**, which starts flow 1 with `suggestedPrompt`. See
   [HUMOR.md](HUMOR.md).

## Safety

- The socket accepts connections from the same user only.
- Paths the agent passes are used as given; the agent already runs as the user.
- A proposal never saves a file on its own. `save` and `export` are the only
  methods that write files, and they write only where the agent asks.
- The Esc and Enter bar stays until the user acts. An agent can't accept its own
  proposal.
