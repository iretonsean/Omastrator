# AI features: design

How the plan in [AI-ROADMAP.md](AI-ROADMAP.md) is built. Omastrator bundles no
model. It launches the user's Omarchy default agent and exposes the open
document to that agent.

## Pieces

```
Omastrator (GUI)                            the agent (claude, codex, opencode, pi…)
┌──────────────────────────────┐            run headless by AgentLauncher, no terminal
│ AgentServer (QLocalServer)   │◀─ JSON-RPC ─ `omastrator agent <method> [json]`   (any agent: shell)
│   $XDG_RUNTIME_DIR/          │◀─ JSON-RPC ─ `omastrator --mcp`                    (MCP stdio bridge)
│   omastrator.sock            │
│ AgentTools ─▶ EditorSession  │
│ AgentProposal (preview)      │
│ AgentLauncher ─▶ the agent   │
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
  "Choose an agent in Omarchy → Setup → Default → Agent". It then runs the
  agent itself, headless, in the background (see [Headless runs](#headless-runs)),
  with arguments passed directly (never through a shell), and sets
  `OMASTRATOR_BIN` and `OMASTRATOR_SOCKET` in the environment. An agent it
  doesn't know how to run headless, or any agent when the setting
  `agent/showTerminal` is on (Help ▸ Connect an Agent ▸ "Open the agent in a
  terminal while it works"), goes through `omarchy agent prompt "<prompt>"`
  instead, in a terminal. The working directory is
  `~/.local/share/omastrator/agent/`, which contains `AGENTS.md` and
  `CLAUDE.md` (how to drive Omastrator with the CLI), written fresh on every
  launch. It holds no `.mcp.json`: an MCP server there made Claude Code ask for
  approval on every run, so an old one is removed. Each prompt names the task,
  the id of the request it answers, and the CLI by its absolute path.

## Headless runs

The user sees Omastrator doing the work, not a terminal. `AgentLauncher::launch`
starts the agent as a background `QProcess` in its own process group, with
stdin closed and no MCP servers. What it may do without asking depends on the
**access level**:

- `AgentAccess::omastrator` (Generate, Refine, Edit with Instruction,
  Vectorize with AI, Roast My Design): read the files it is given and run the
  Omastrator CLI, nothing else.
- `AgentAccess::project` (Live's write-back, Hand to Agent and Deploy with
  agent, through `LaunchOptions{AgentAccess::project, <worktree or folder>,
  "live" / "handoff" / "deploy", …}`): read, edit and write files and run shell
  commands, starting in the project's worktree (Deploy: its checkout). A Live
  run that ends without `live agentDone` says so in the Live panel with Show
  log, or fails the deploy's Writing… stage, and its worktree is removed; a
  deploy run that ends without `live_deployed` is "Deploy failed: …", with
  Details opening the run's log. Cancel stops them.

The commands (`<cli>` is `<absolute path to omastrator> agent`):

| Agent | Omastrator access | Project access |
|---|---|---|
| `claude` | `claude -p <prompt> --output-format text --no-session-persistence --strict-mcp-config --mcp-config '{"mcpServers":{}}' --permission-mode acceptEdits --tools Bash,Read --allowedTools "Bash(<cli> *)" Read` | the same with `--tools Bash,Read,Edit,Write,Glob,Grep --allowedTools Bash Read Edit Write Glob Grep` |
| `codex` | `codex exec --skip-git-repo-check --ephemeral --color never -c approval_policy="never" --sandbox workspace-write -c sandbox_workspace_write.network_access=true -- <prompt>` (the read-only sandbox refuses the Unix socket the CLI needs; writes stay in the working folder) | the same |
| `opencode` | `opencode run <prompt>`, with `OPENCODE_CONFIG_CONTENT` denying edits, web fetches and every command but `<cli> *` | edits and commands allowed |
| `gemini` | `gemini -p <prompt> --output-format text --skip-trust --approval-mode default --allowed-tools read_file "run_shell_command(<cli>)"` | `--approval-mode auto_edit --allowed-tools run_shell_command` |

Nothing is ever run with `--dangerously-skip-permissions` or its equivalents.
Anything outside the allowlist is refused without a prompt, and the agent
carries on or stops.

- **Logs.** Each run's stdout and stderr go to
  `$XDG_STATE_HOME/omastrator/agent-runs/<timestamp>-<task>.log` (by default
  `~/.local/state/…`), and the newest 30 are kept. The log names the agent, the
  folder and the command, with the task itself left out; the environment is
  never written.
- **Timeout.** A run is stopped (SIGTERM to its group, then SIGKILL) after 5
  minutes, or 20 for project tasks; the setting `agent/timeoutSeconds`
  changes both.
- **In the app.** While a run works, the panel or the proposal bar shows
  "Claude is roasting… 12 s" (generating, editing, tracing) with Cancel, which
  stops the run. If the run ends before its answer (`show_roast`,
  `show_variations`, `proposal_finish`) arrives, the same place shows one plain
  line, "Claude stopped without an answer." (with the last meaningful line of
  its stderr after a colon, when there is one) or "Claude ran out of time after
  5 minutes and was stopped.", and **Show log** opens the log with `xdg-open`.
  `status_get` carries that line as `error` and the log as `log`.

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
- `document_get {page?}`: the current page (or the `page` given: an id, a name
  or `"all"`) as `DocumentCodec` JSON, plus `pages` (`[{id, name, current}]`),
  `selection` and `activeLayer`. A one-page document has one page.
- `selection_get {}`: the selected objects' JSON and their bounds.
- `render {scale?=1, selectionOnly?=false, path?, page?}`: renders a PNG of the
  first artboard of the current page (or of `page`), whatever the page count. It is written
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
- `arrange {ids?, order}`, `align {ids?, edge, target?, page?}` (the artboard is the page's active one),
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
  transparent?}`. A PDF holds every artboard that exports, across every page,
  and the reply counts them (`sheets`). PNG, JPEG and SVG write the current
  page's first artboard that is set to export and name it in the reply
  (`artboard`). A board set not to export is skipped, and if none exports the
  call fails. `document_get` lists
  each artboard with `exported`, so the agent can tell which one that will be.
  `render` is a view, not an export: it draws the page's first artboard whether
  or not it exports.
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
- `page {action, page?, name?, index?, ids?}`: `add`, `rename`, `duplicate`,
  `reorder`, `move_objects` or `show`, the designer's own page operations. Each
  is a normal named undo step, not part of the proposal (so it waits while a
  proposal is open, like `apply_color`). There is no delete. Returns the page
  and the list of pages.
- `status_get {}`: the tool, whether a document is open, the proposal title
  and summary, the agent task waited on (`waiting`, `task`, `agent`), the
  newest variations (`variations`, `variationsId`), `roastId`, `ready`
  (results the user hasn't acted on) and `error`.
- `status_follow {}` is answered on the socket only, not over MCP: it returns
  `status_get`'s answer, then sends a `{"method": "status", "params": …}`
  notification each time that answer changes. `omastrator status --follow`
  uses it; see [OS-SUITE.md](OS-SUITE.md).

**Desktop** (the Capture tab and `omastrator island capture …`; see [OS-SUITE.md](OS-SUITE.md))

These are the user's own actions, so each is a normal undo step, not a
proposal. `tools/list` leaves them out and `tools/call` refuses them, so an
agent can't use them to skip the accept bar. `swatches_get` is the exception:
it only reads.
- `apply_color {color, target?: fill|stroke}`
- `swatches_get {}`, `swatches_add {group?, swatches: [{name?, color}], replace?}`
- `open_capture {path, trace?, colors?}`: a screenshot as a new document,
  traced.
- `paste_svg {svg, name?}`
- `new_document {}`, `show_panel {panel}`: bring the window forward.
- `command {name, …}`: the user's own commands for voice (undo, redo, zoom,
  select all, group, delete, duplicate, arrange, align, distribute, fill,
  stroke, strokeWidth, opacity), each a normal undo step.
- `live {action, …}`: Live mode ([OS-SUITE.md](OS-SUITE.md)). Actions:
  `start`, `stop`, `select`, `edit`, `status`, `screenshot`, `writeBack`,
  `ask`, `agentDone`, `review`, `discard`, `save`, `deploy`, `cancel`,
  `history`, `restore`, `details`, `remember`, `github`. An agent given a Live
  task works in a git worktree and ends with
  `omastrator agent live '{"action": "agentDone", "requestId": "…",
  "summary": "…"}'`; its change is written and recorded, and Review changes
  shows the diff.
- `live_deployed {requestId?, url?, command?, error?}`: ends a Live deploy
  task (Deploy with agent): where it's live, and a command to deploy with next
  time, which must not contain a secret.
- `ai_start {flow: generate|edit|roast|vectorize|cancel, prompt?, count?,
  fitToSelection?, mode?: logo|sketch}`: the Ask field and `omastrator island ai …`. Generate and
  Edit open their sheet unless a prompt is given; Vectorize takes the selected
  image, else the screenshot Capture last traced.
- `design {action, …}`: design mode everywhere ([ANYWHERE.md](ANYWHERE.md)).
  Actions: `on`, `off`, `toggle`, `status`, `tool`, `alt`, `select`,
  `selectArt`, `deselect`, `measure`, `draw`, `action`, `ask`, `keep`,
  `discard`, `send`, `undo`, `redo`, `clear`, `onboarding`, `desk`. Its Ask
  launches the agent on the overlay drawn over a surface: the agent's edit
  methods then act on the overlay, as a proposal the bar keeps or discards.
- `show_window {files?, raise?}`: brings the window forward (the app may run in
  the background without one) with the files opened. `quit_app {}` quits,
  asking about unsaved documents first.

## Flows

1. **Generate…** (Object ▸ Generate…). A prompt, a variation count from 1 to 6
   (default 3), and optionally "fit to selection". The agent is launched with
   the brief. It calls `show_variations`, and the Variations panel shows
   thumbnails rendered by `VectorRenderer`. Picking one inserts it as a
   proposal. **Refine** takes an instruction ("rounder", "fewer colours") and
   relaunches with the chosen SVG and the history of the conversation. The
   result is a new set of variations to pick from.
2. **Edit with instruction** (Object ▸ Edit with Instruction…, the
   contextual task bar's Ask AI… field, or a request typed into Ctrl+K). The
   instruction runs on the selection, or on the document when nothing is
   selected. The agent calls the edit methods, then `proposal_finish`. From
   Ctrl+K with nothing selected, a request for new art goes to Generate
   instead.
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
- A locked document (File ▸ Lock Document) can't be changed by an agent: every
  edit fails with error `-32004` and says to ask the user to unlock it. Reading,
  selecting, `save` and `export` still work, and only the user can unlock.
