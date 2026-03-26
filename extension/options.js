import { VaultKeySerial } from "./serial.js";

const el = (id) => document.getElementById(id);

let serial = null;
let unlocked = false;
let creds = [];

function setStatus(msg) {
  el("status").textContent = msg || "";
}

function setView(which) {
  el("section-unlock").classList.toggle("hidden", which !== "locked");
  el("section-vault").classList.toggle("hidden", which !== "unlocked");
}

function escapeHtml(s) {
  return String(s).replaceAll("&","&amp;").replaceAll("<","&lt;").replaceAll(">","&gt;").replaceAll('"',"&quot;");
}

async function loadSettings() {
  const { settings } = await chrome.storage.local.get({ settings: { autoLockMs: 120000, defaultHidMode: 1 } });
  el("auto-lock").value = String(settings.autoLockMs ?? 120000);
  el("hid-mode").value = String(settings.defaultHidMode ?? 1);
}

async function saveSettings() {
  const settings = {
    autoLockMs: Number(el("auto-lock").value),
    defaultHidMode: Number(el("hid-mode").value)
  };
  await chrome.storage.local.set({ settings });
  setStatus("Saved.");
}

async function safeCommand(cmdObj) {
  if (!serial) throw new Error("not connected");
  const res = await serial.command(cmdObj, 3000);
  if (!res || typeof res !== "object") throw new Error("bad_response");
  if (res.status === "error") throw new Error(res.message || "error");
  return res;
}

async function connect() {
  setStatus("");
  // Clean up any previous connection first
  if (serial) {
    try { await serial.disconnect(); } catch {}
    serial = null;
  }
  const s = new VaultKeySerial();
  s.onDisconnect(() => {
    serial = null;
    unlocked = false;
    creds = [];
    setView("none");
    setStatus("Disconnected.");
  });
  try {
    await s.connect();
  } catch (e) {
    setStatus("Port selection failed: " + (e.message || e));
    return;
  }
  serial = s;
  try {
    await safeCommand({ cmd: "ping" });
  } catch (e) {
    setStatus("Connected to port but device not responding: " + (e.message || e) + "\nMake sure firmware is flashed and ESP32 is running.");
    // Keep serial open so user can retry or disconnect
    setView("locked");
    return;
  }
  setView("locked");
  setStatus("Connected. Enter your PIN to unlock.");
}

async function disconnect() {
  if (serial) await serial.disconnect();
  serial = null;
  unlocked = false;
  creds = [];
  setView("none");
  setStatus("");
}

async function unlock() {
  setStatus("");
  const pin = el("pin").value.trim();
  if (!pin) { setStatus("Enter a PIN."); return; }
  const res = await safeCommand({ cmd: "unlock", pin });
  try { await safeCommand({ cmd: "sync_time", timestamp: Math.floor(Date.now() / 1000) }); } catch {}
  el("pin").value = "";
  unlocked = true;
  setView("unlocked");
  await refresh();
  setStatus(`Unlocked. Token: ${(res.token || "").slice(0, 10)}...`);
}

async function lock() {
  try { await safeCommand({ cmd: "lock" }); } catch {}
  unlocked = false;
  setView("locked");
  setStatus("Locked.");
}

async function refresh() {
  setStatus("");
  const res = await safeCommand({ cmd: "list" });
  creds = Array.isArray(res.credentials) ? res.credentials : [];
  renderList();
  setStatus(`${creds.length} credential(s) loaded.`);
}

function renderList() {
  const q = (el("search").value || "").trim().toLowerCase();
  const list = el("cred-list");
  list.innerHTML = "";
  const filtered = creds.filter(c => {
    if (!q) return true;
    return (c.service||"").toLowerCase().includes(q) ||
           (c.url||"").toLowerCase().includes(q) ||
           (c.username||"").toLowerCase().includes(q);
  });
  if (filtered.length === 0) {
    list.innerHTML = '<div class="muted">No credentials found.</div>';
    return;
  }
  for (const c of filtered) {
    const item = document.createElement("div");
    item.style.cssText = "padding:10px;border-bottom:1px solid var(--border);display:flex;justify-content:space-between;align-items:center;";
    item.innerHTML = `
      <div>
        <div style="font-weight:650;font-size:14px">${escapeHtml(c.service || "(unnamed)")}</div>
        <div class="muted">${escapeHtml(c.url || "")} &middot; ${escapeHtml(c.username || "-")}</div>
      </div>
      <button data-act="select" class="primary" style="white-space:nowrap">Select on device</button>
    `;
    item.querySelector('[data-act="select"]').addEventListener("click", async () => {
      await safeCommand({ cmd: "select", id: c.id });
      setStatus(`Selected "${c.service}" on device.`);
    });
    list.appendChild(item);
  }
}

async function testConnection() {
  setStatus("");
  if (!serial) {
    serial = new VaultKeySerial();
    serial.onDisconnect(() => (serial = null));
    await serial.connect({ auto: true });
  }
  const t0 = performance.now();
  await safeCommand({ cmd: "ping" });
  const dt = Math.round(performance.now() - t0);
  setStatus(`Ping OK in ${dt}ms`);
}

// Wiring
el("btn-connect").addEventListener("click", () => connect().catch((e) => setStatus(String(e && e.message ? e.message : e))));
el("btn-test").addEventListener("click", () => testConnection().catch((e) => setStatus(String(e && e.message ? e.message : e))));
el("btn-save").addEventListener("click", () => saveSettings().catch((e) => setStatus(String(e && e.message ? e.message : e))));
el("btn-disconnect").addEventListener("click", () => disconnect().catch((e) => setStatus(String(e && e.message ? e.message : e))));
el("btn-unlock").addEventListener("click", () => unlock().catch((e) => setStatus(String(e && e.message ? e.message : e))));
el("pin").addEventListener("keydown", (e) => { if (e.key === "Enter") unlock().catch(err => setStatus(String(err && err.message ? err.message : err))); });
el("btn-lock").addEventListener("click", () => lock().catch((e) => setStatus(String(e && e.message ? e.message : e))));
el("btn-refresh").addEventListener("click", () => refresh().catch((e) => setStatus(String(e && e.message ? e.message : e))));
el("search").addEventListener("input", renderList);

await loadSettings();

