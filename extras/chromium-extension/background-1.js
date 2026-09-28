// Omastrator in your own Chromium (docs/OS-SUITE.md, "Live in your own browser").
// Keep this filename versioned: Chromium caches the service workers of extensions
// loaded with --load-extension, and a new name makes it register new code.
//
// One native port to `omastrator browser-host`, which relays to Omastrator. Over it:
//   - Omastrator's DevTools commands, run with chrome.debugger on the tab Live joined
//     (its session is "tab-<id>"), and the tab's events back;
//   - the side panel's calls to Omastrator, and Omastrator's status to the panel.
// An open native port also keeps this worker alive.

const HOST = "io.github.iretonsean.omastrator";

let port = null;
// Omastrator answers through the host (the host itself may be there without it).
let linked = false;
// Why the host couldn't start: usually that `omastrator setup` hasn't installed it.
let hostError = "";
let status = null;
let retry = 1000;

// sessionId → tabId, for the tabs Live joined.
const sessions = new Map();
const panels = new Set();
// The worker's call id → the panel that made the call, and the panel's own id for it.
const calls = new Map();
let nextCall = 1;

function connect() {
  try {
    port = chrome.runtime.connectNative(HOST);
  } catch (error) {
    port = null;
    hostError = String(error && error.message || error);
    broadcast();
    return schedule();
  }
  port.onMessage.addListener(fromHost);
  port.onDisconnect.addListener(() => {
    hostError = chrome.runtime.lastError ? chrome.runtime.lastError.message : "";
    port = null;
    setLinked(false);
    schedule();
  });
}

function schedule() {
  setTimeout(connect, retry);
  retry = Math.min(retry * 2, 30000);
}

function toHost(message) {
  if (port) port.postMessage(message);
}

function setLinked(value) {
  if (linked === value) return;
  linked = value;
  if (!linked) {
    status = null;
    // Without Omastrator nothing can answer the overlay: take it out and let the tabs go.
    for (const sessionId of [...sessions.keys()]) release(sessionId, true);
  }
  broadcast();
}

function fromHost(message) {
  if (message.type === "link") {
    hostError = "";
    retry = 1000;
    setLinked(!!message.connected);
  } else if (message.type === "status") {
    status = message.status;
    broadcast();
  } else if (message.type === "reply") {
    const call = calls.get(message.id);
    calls.delete(message.id);
    if (call && panels.has(call.panel)) call.panel.postMessage(Object.assign({}, message, { id: call.id }));
  } else if (message.type === "cdp") {
    runCommand(message);
  }
}

// ------------------------------------------------------------ DevTools

function answer(id, result, error) {
  toHost(error ? { type: "cdp", id, error: { message: String(error) } } : { type: "cdp", id, result: result || {} });
}

function lastError() {
  return chrome.runtime.lastError ? chrome.runtime.lastError.message : "";
}

async function activeTab() {
  const [tab] = await chrome.tabs.query({ active: true, lastFocusedWindow: true });
  return tab || null;
}

async function runCommand({ id, method, params = {}, sessionId }) {
  try {
    if (method === "Omastrator.activeTab") {
      const tab = await activeTab();
      return tab ? answer(id, { tabId: tab.id, url: tab.url || "", title: tab.title || "" }) : answer(id, null, "No tab is open.");
    }
    if (method === "Omastrator.attach") {
      const tab = params.tabId ? await chrome.tabs.get(params.tabId) : await activeTab();
      if (!tab) return answer(id, null, "No tab is open.");
      const session = "tab-" + tab.id;
      if (!sessions.has(session)) {
        await new Promise((resolve, reject) => chrome.debugger.attach({ tabId: tab.id }, "1.3", () => {
          const error = lastError();
          error ? reject(new Error(error)) : resolve();
        }));
        sessions.set(session, tab.id);
      }
      return answer(id, { tabId: tab.id, sessionId: session, url: tab.url || "", title: tab.title || "" });
    }
    if (method === "Omastrator.detach") {
      release(params.sessionId, false);
      return answer(id, {});
    }
    const tabId = sessions.get(sessionId);
    if (tabId === undefined) return answer(id, null, "Omastrator isn't attached to that tab.");
    chrome.debugger.sendCommand({ tabId }, method, params, (result) => {
      const error = lastError();
      answer(id, result, error);
    });
  } catch (error) {
    answer(id, null, error && error.message || error);
  }
}

// Lets a tab go; `quietly` when Omastrator is gone and the overlay must come out here.
function release(sessionId, quietly) {
  const tabId = sessions.get(sessionId);
  if (tabId === undefined) return;
  sessions.delete(sessionId);
  const detach = () => chrome.debugger.detach({ tabId }, () => void chrome.runtime.lastError);
  if (!quietly) return detach();
  chrome.debugger.sendCommand({ tabId }, "Runtime.evaluate", { expression: "window.__oma && window.__oma.leave()" }, () => {
    void chrome.runtime.lastError;
    detach();
  });
}

chrome.debugger.onEvent.addListener((source, method, params) => {
  const sessionId = "tab-" + source.tabId;
  if (sessions.has(sessionId)) toHost({ type: "cdp", method, params: params || {}, sessionId });
});

// The "is debugging this browser" bar's Cancel, a closed tab, or DevTools taking over: Live ends.
chrome.debugger.onDetach.addListener((source, reason) => {
  const sessionId = "tab-" + source.tabId;
  if (!sessions.delete(sessionId)) return;
  toHost({ type: "cdp", method: "Omastrator.detached", params: { sessionId, reason }, sessionId: "" });
});

// ------------------------------------------------------------ the side panel

function stateFor() {
  return { type: "state", host: !!port, hostError, linked, status };
}

function broadcast() {
  const state = stateFor();
  for (const panel of panels) panel.postMessage(state);
}

chrome.runtime.onConnect.addListener((panel) => {
  if (panel.name !== "panel") return;
  panels.add(panel);
  panel.postMessage(stateFor());
  panel.onDisconnect.addListener(() => {
    panels.delete(panel);
    for (const [id, call] of calls) if (call.panel === panel) calls.delete(id);
  });
  panel.onMessage.addListener((message) => {
    if (message.type === "wake") return toHost({ type: "wake" });
    if (message.type !== "call") return;
    if (!port || !linked) {
      return panel.postMessage({ type: "reply", id: message.id, error: port ? "Omastrator isn't running." : "Omastrator's browser host isn't installed." });
    }
    // Ids are the worker's own, so two panels never collide.
    const id = nextCall++;
    calls.set(id, { panel, id: message.id });
    toHost({ type: "call", id, method: message.method, params: message.params || {} });
  });
});

chrome.sidePanel.setPanelBehavior({ openPanelOnActionClick: true }).catch(() => {});

connect();
