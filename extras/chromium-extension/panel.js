// The side panel: Live's start panel for the tab in front, then the session's
// state and actions. Everything goes to Omastrator through the service worker.

const $ = (id) => document.getElementById(id);
const worker = chrome.runtime.connect({ name: "panel" });

let state = { host: false, hostError: "", linked: false, status: null };
let tab = null;
let foldersFor = "";
let nextId = 1;
const waiting = new Map();

function call(method, params) {
  return new Promise((resolve, reject) => {
    const id = nextId++;
    waiting.set(id, { resolve, reject });
    worker.postMessage({ type: "call", id, method, params });
  });
}

function live(action, params) {
  return call("live", Object.assign({ action }, params || {}));
}

worker.onMessage.addListener((message) => {
  if (message.type === "state") {
    state = message;
    render();
  } else if (message.type === "reply") {
    const pending = waiting.get(message.id);
    waiting.delete(message.id);
    if (!pending) return;
    message.error ? pending.reject(new Error(message.error)) : pending.resolve(message.result || {});
  }
});

function showError(error) {
  $("error").textContent = error ? String(error.message || error) : "";
  $("error").hidden = !error;
}

// Runs an action from a button: the button waits, a refusal is said under it.
async function act(button, run) {
  showError("");
  button.disabled = true;
  try {
    await run();
  } catch (error) {
    showError(error);
  } finally {
    button.disabled = false;
  }
}

// ------------------------------------------------------------ the tab in front

async function refreshTab() {
  const [front] = await chrome.tabs.query({ active: true, currentWindow: true });
  tab = front || null;
  render();
}

chrome.tabs.onActivated.addListener(refreshTab);
chrome.tabs.onUpdated.addListener((id, change) => {
  if (tab && id === tab.id && (change.url || change.title || change.status === "complete")) refreshTab();
});

function webPage(url) {
  return /^(https?|file):/.test(url || "");
}

async function loadFolders() {
  const url = tab && tab.url;
  if (!url || !webPage(url) || foldersFor === url || !state.linked) return;
  foldersFor = url;
  const list = $("folders");
  list.textContent = "";
  let folders = [];
  try {
    folders = (await live("folders", { url })).folders || [];
  } catch (error) {
    foldersFor = "";
    return;
  }
  for (const [index, entry] of folders.entries()) {
    const label = document.createElement("label");
    label.className = "choice";
    const radio = document.createElement("input");
    radio.type = "radio";
    radio.name = "folder";
    radio.value = entry.folder;
    radio.checked = index === 0;
    const text = document.createElement("span");
    text.textContent = entry.folder.replace(/^\/home\/[^/]+/, "~");
    const why = document.createElement("span");
    why.className = "why";
    why.textContent = entry.reason || "";
    text.appendChild(why);
    label.append(radio, text);
    list.appendChild(label);
  }
  // Nothing likely: a mock-up until the user names the code.
  if (!folders.length) document.querySelector('input[name=folder][value=""]').checked = true;
}

function chosenFolder() {
  const chosen = document.querySelector("input[name=folder]:checked");
  if (!chosen) return "";
  if (chosen.value !== "other") return chosen.value;
  return $("otherFolder").value.trim();
}

$("otherFolder").addEventListener("focus", () => { document.querySelector('input[name=folder][value="other"]').checked = true; });

// ------------------------------------------------------------ what the panel shows

function render() {
  const live = state.status && state.status.live;
  const offline = !state.linked;
  $("offline").hidden = !offline;
  if (offline) {
    $("offlineText").textContent = !state.host
      ? "Omastrator's browser host isn't installed. Run `omastrator setup` in a terminal, then restart Chromium."
        + (state.hostError ? "\n\n" + state.hostError : "")
      : "Omastrator isn't running.";
    $("wake").hidden = !state.host;
  }
  const inTab = !!(live && live.tab && live.state !== "off");
  const elsewhere = !!(live && !live.tab && (live.state === "running" || live.state === "starting"));
  $("running").hidden = offline || !inTab;
  $("elsewhere").hidden = offline || !elsewhere;
  $("start").hidden = offline || inTab || elsewhere;

  if (!$("start").hidden) {
    $("title").textContent = tab ? tab.title || "Untitled" : "No tab";
    $("url").textContent = tab ? tab.url || "" : "";
    const usable = !!(tab && webPage(tab.url));
    $("cannot").hidden = usable || !tab;
    $("code").hidden = !usable;
    $("begin").disabled = !usable;
    if (live && live.state === "failed") showError("Live couldn't start: " + live.message);
    loadFolders();
  }
  if (!$("running").hidden) {
    $("liveUrl").textContent = live.url || "";
    $("liveProject").textContent = live.mockup ? "None: a mock-up, changes stay in this browser" : (live.project || "").replace(/^\/home\/[^/]+/, "~");
    const unsaved = live.unsaved ? `, ${live.unsaved} file${live.unsaved === 1 ? "" : "s"} written, not saved` : "";
    $("liveEdits").textContent = `${live.edits || 0} on the page${unsaved}`;
    const deploy = live.deploy || {};
    const lines = [live.state === "starting" ? live.message : "", live.liveMessage || "", deploy.message || ""].filter(Boolean);
    $("liveMessage").textContent = lines.join("\n");
    $("liveMessage").className = deploy.failed ? "error" : "message";
    $("liveButtons").hidden = live.state !== "running" || !!live.mockup;
    $("deploy").disabled = !!deploy.running;
    $("stop").textContent = live.state === "starting" ? "Cancel" : "Stop Live";
  }
  if (!$("elsewhere").hidden)
    $("elsewhereText").textContent = `Live is ${live.state === "starting" ? "starting" : "running"} in Omastrator's own browser${live.url ? " on " + live.url : ""}.`;
}

// ------------------------------------------------------------ buttons

$("wake").addEventListener("click", () => {
  worker.postMessage({ type: "wake" });
  $("offlineText").textContent = "Starting Omastrator…";
});
$("begin").addEventListener("click", (event) => act(event.target, async () => {
  const folder = chosenFolder();
  const choice = document.querySelector("input[name=folder]:checked");
  if (choice && choice.value === "other" && !folder) throw new Error("Type the folder the page's code is in.");
  await live("start", { tab: tab.id, folder });
}));
$("stop").addEventListener("click", (event) => act(event.target, () => live("stop")));
$("stopElsewhere").addEventListener("click", (event) => act(event.target, () => live("stop")));
$("writeBack").addEventListener("click", (event) => act(event.target, () => live("writeBack")));
$("save").addEventListener("click", (event) => act(event.target, () => live("save")));
$("deploy").addEventListener("click", (event) => act(event.target, () => live("deploy")));

refreshTab();
