# Handoff: headless agent runs

Branch `worktree-agent-a309a81b931e1d3ca`. The design is in
[../AI-DESIGN.md](../AI-DESIGN.md#headless-runs).

## Done

- [x] `AgentLauncher::launch` runs claude, codex, opencode and gemini as a
  background `QProcess` in its own process group: no terminal, no MCP servers,
  and an allowlist per `AgentAccess` (`omastrator` or `project`). Other agents
  and the `agent/showTerminal` setting use `omarchy agent prompt`.
- [x] `.mcp.json` is no longer written to the agent folder, and an old one is
  removed. Prompts and AGENTS.md name the CLI by its absolute path, which is
  what Claude's `Bash(<cli> *)` rule allows.
- [x] Logs in `$XDG_STATE_HOME/omastrator/agent-runs/`, the last 30 kept; the
  task and the environment are never written.
- [x] Timeout: 5 minutes (20 for project tasks); `agent/timeoutSeconds`
  overrides it.
- [x] The panels and proposal bar show "Claude is roasting… 12 s" with Cancel
  (kills the run); a run without an answer shows one line and Show log.
- [x] Help ▸ Connect an Agent: "Open the agent in a terminal while it works".
- [x] Tests: fake claude, codex, opencode and gemini on a temporary PATH
  (`tests/Agent/FakeAgents.h`); AgentUiTests is also the CLI they call back
  through. The full suite passes (41 of 41).
- [x] End to end with the real Claude Code, offscreen: a roast arrived in
  32 s and two variations in 37 s, with no new window and no MCP prompt.

## Not done

- [ ] Live (`AgentBridge+Live.cpp`) still calls `launchIn`, which now runs
  headless with project access but without a finished callback, so Cancel
  doesn't stop a Live run and a Live run that stops without `agentDone` isn't
  reported. Switch it to `AgentLauncher::launch(prompt, socket,
  {AgentAccess::project, worktree, "live", 0, callback}, &run)`, keep the run,
  and report from the callback as `AgentBridge::runFinished` does.
- [x] Codex: `codex sandbox` showed the read-only sandbox refuses Unix
  sockets, so both access levels use `workspace-write` with
  `network_access=true`. Not run end to end with the real Codex, opencode or
  Gemini (only Claude Code).
