import { VaultKeySerial } from "./serial.js";

const el = (id) => document.getElementById(id);

let serial = null;
let unlocked = false;
let creds = [];
let activeTotpId = null;
let editTotpId = null;
let editTotpStatus = "";
let totpTimer = null;
let totpPending = false;
let totpDisplay = { code: "------", meta: "Open TOTP to load a code.", error: "" };

function clearTotpTimer() {
  if (totpTimer) clearInterval(totpTimer);
  totpTimer = null;
  totpPending = false;
}

function stopTotpLoop() {
  clearTotpTimer();
  activeTotpId = null;
  totpDisplay = { code: "------", meta: "Open TOTP to load a code.", error: "" };
}

function closeTotpEditor() {
  editTotpId = null;
  editTotpStatus = "";
}

function setTotpDisplay(next) {
  totpDisplay = {
    code: "------",
    meta: "",
    error: "",
    ...next,
  };
}

function describeTotpError(msg) {
  if (msg === "no_totp") return "No TOTP secret stored for this credential.";
  if (msg === "invalid_secret") return "Stored TOTP secret is invalid.";
  if (msg === "no_time") return "Device time is not set. Unlock again to sync time.";
  if (msg === "locked") return "Vault is locked.";
  if (msg === "not_found") return "Credential not found.";
  return msg || "Unable to load TOTP.";
}

function setStatus(msg) {
  el("status").textContent = msg || "";
}

function setAddStatus(msg) {
  const node = el("add-status");
  if (node) node.textContent = msg || "";
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
  if (document.getElementById("add-hid-mode")) {
    el("add-hid-mode").value = String(settings.defaultHidMode ?? 1);
  }
}

async function saveSettings() {
  const settings = {
    autoLockMs: Number(el("auto-lock").value),
    defaultHidMode: Number(el("hid-mode").value)
  };
  await chrome.storage.local.set({ settings });
  if (document.getElementById("add-hid-mode")) {
    el("add-hid-mode").value = String(settings.defaultHidMode ?? 1);
  }
  setStatus("Saved.");
}

function clearAddForm() {
  el("add-service").value = "";
  el("add-url").value = "";
  el("add-username").value = "";
  el("add-password").value = "";
  el("add-totp").value = "";
  el("add-hid-mode").value = String(el("hid-mode").value || "1");
  setAddStatus("");
}

async function safeCommand(cmdObj) {
  if (!serial) throw new Error("not connected");
  const res = await serial.command(cmdObj, 3000);
  if (!res || typeof res !== "object") throw new Error("bad_response");
  if (res.status === "error") {
    const err = new Error(res.message || "error");
    err.device = res;
    throw err;
  }
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
    stopTotpLoop();
    closeTotpEditor();
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
    setStatus("Connected, but the device did not respond. Check the firmware and try again.");
    // Keep serial open so user can retry or disconnect
    setView("locked");
    return;
  }
  setView("locked");
  setStatus("Connected. Enter PIN.");
}

async function disconnect() {
  stopTotpLoop();
  closeTotpEditor();
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
  await safeCommand({ cmd: "unlock", pin });
  try { await safeCommand({ cmd: "sync_time", timestamp: Math.floor(Date.now() / 1000) }); } catch {}
  el("pin").value = "";
  unlocked = true;
  setView("unlocked");
  await refresh();
  setStatus("Unlocked.");
}

async function lock() {
  try { await safeCommand({ cmd: "lock" }); } catch {}
  stopTotpLoop();
  closeTotpEditor();
  unlocked = false;
  setView("locked");
  setStatus("Locked.");
}

async function refresh() {
  setStatus("");
  const res = await safeCommand({ cmd: "list" });
  creds = Array.isArray(res.credentials) ? res.credentials : [];
  if (activeTotpId != null && !creds.some((c) => c.id === activeTotpId)) {
    stopTotpLoop();
  }
  if (editTotpId != null && !creds.some((c) => c.id === editTotpId)) {
    closeTotpEditor();
  }
  renderList();
  setStatus(`${creds.length} credential(s) loaded.`);
}

async function saveManual() {
  setAddStatus("");
  const service = el("add-service").value.trim();
  const password = el("add-password").value;
  if (!service) { setAddStatus("Service name is required."); return; }
  if (!password) { setAddStatus("Password is required."); return; }

  const url = el("add-url").value.trim();
  const username = el("add-username").value.trim();

  // Check if credential with same service, username, and URL already exists
  const existingCred = creds.find(c => 
    c.service === service && 
    c.username === username && 
    c.url === url
  );

  // If exists, delete the old one first
  if (existingCred) {
    try {
      await safeCommand({ cmd: "delete", id: existingCred.id });
      setAddStatus("Updated existing credential.");
    } catch (e) {
      setAddStatus("Error updating credential: " + String(e && e.message ? e.message : e));
      return;
    }
  }

  const payload = {
    cmd: "add",
    service,
    url,
    username,
    password,
    totp_secret: el("add-totp").value.trim(),
    hid_mode: Number(el("add-hid-mode").value),
  };

  await safeCommand(payload);
  clearAddForm();
  await refresh();
  const action = existingCred ? "Updated" : "Added";
  setStatus(`${action} ${payload.service}.`);
}

async function clearAllCredentials() {
  if (!confirm("Are you sure you want to delete all credentials? This cannot be undone.")) {
    return;
  }
  setStatus("Deleting all credentials...");
  try {
    for (const cred of creds) {
      await safeCommand({ cmd: "delete", id: cred.id });
    }
    await refresh();
    setStatus("All credentials deleted.");
  } catch (e) {
    setStatus("Error deleting credentials: " + String(e && e.message ? e.message : e));
  }
}

async function deleteCredential(id) {
  const cred = creds.find(c => c.id === id);
  if (!cred) return;
  
  if (!confirm(`Delete credential for "${cred.service}"?`)) {
    return;
  }
  
  try {
    await safeCommand({ cmd: "delete", id });
    await refresh();
    setStatus(`Deleted "${cred.service}".`);
  } catch (e) {
    setStatus("Error deleting credential: " + String(e && e.message ? e.message : e));
  }
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
  if (activeTotpId != null && !filtered.some((c) => c.id === activeTotpId)) {
    stopTotpLoop();
  }
  if (editTotpId != null && !filtered.some((c) => c.id === editTotpId)) {
    closeTotpEditor();
  }
  if (filtered.length === 0) {
    list.innerHTML = '<div class="muted">No credentials found.</div>';
    return;
  }
  for (const c of filtered) {
    const totpOpen = activeTotpId === c.id;
    const editOpen = editTotpId === c.id;
    const metaClass = totpDisplay.error ? "totp-meta totp-error" : "totp-meta";
    const item = document.createElement("div");
    item.className = "cred-item";
    item.dataset.id = String(c.id);
    item.innerHTML = `
      <div class="cred-head">
        <div style="min-width: 0">
          <div class="cred-title">${escapeHtml(c.service || "(unnamed)")}</div>
          <div class="muted cred-sub">${escapeHtml(c.url || "")} &middot; ${escapeHtml(c.username || "-")}</div>
        </div>
        <div class="row">
          <button data-act="select" class="primary" style="white-space:nowrap">Select</button>
          <button data-act="totp" style="white-space:nowrap">${totpOpen ? "Hide TOTP" : "TOTP"}</button>
          <button data-act="edit-totp" style="white-space:nowrap">${c.has_totp ? (editOpen ? "Cancel TOTP" : "Replace TOTP") : (editOpen ? "Cancel TOTP" : "Add TOTP")}</button>
          <button data-act="delete" class="danger" style="white-space:nowrap">Delete</button>
        </div>
      </div>
      ${editOpen ? `
        <div class="totp-panel" data-totp-edit-panel>
          <div class="totp-meta">Enter a new Base32 secret. Save blank to remove the stored TOTP secret.</div>
          <div class="row" style="margin-top: 8px">
            <input data-totp-input type="text" placeholder="BASE32SECRET" style="flex: 1" />
          </div>
          <div class="row" style="margin-top: 8px">
            <button data-act="save-totp" class="primary">Save TOTP</button>
            <button data-act="cancel-totp-edit">Cancel</button>
          </div>
          <div class="totp-meta${editTotpStatus ? "" : " hidden"}" data-totp-edit-status>${escapeHtml(editTotpStatus || "")}</div>
        </div>
      ` : ""}
      ${totpOpen ? `
        <div class="totp-panel" data-totp-panel>
          <div class="totp-code" data-code>${escapeHtml(totpDisplay.code || "------")}</div>
          <div class="${metaClass}" data-meta>${escapeHtml(totpDisplay.meta || "Open TOTP to load a code.")}</div>
        </div>
      ` : ""}
    `;
    item.querySelector('[data-act="select"]').addEventListener("click", async () => {
      await safeCommand({ cmd: "select", id: c.id });
      setStatus(`Selected "${c.service}" on device.`);
    });
    item.querySelector('[data-act="totp"]').addEventListener("click", () => {
      toggleTotp(c.id).catch((e) => setStatus(String(e && e.message ? e.message : e)));
    });
    item.querySelector('[data-act="edit-totp"]').addEventListener("click", () => {
      toggleTotpEditor(c.id);
    });
    item.querySelector('[data-act="delete"]').addEventListener("click", () => {
      deleteCredential(c.id).catch((e) => setStatus(String(e && e.message ? e.message : e)));
    });
    const saveBtn = item.querySelector('[data-act="save-totp"]');
    if (saveBtn) {
      saveBtn.addEventListener("click", () => {
        saveTotpEdit(c.id).catch((e) => {
          editTotpStatus = String(e && e.message ? e.message : e);
          renderList();
        });
      });
    }
    const cancelBtn = item.querySelector('[data-act="cancel-totp-edit"]');
    if (cancelBtn) {
      cancelBtn.addEventListener("click", () => {
        closeTotpEditor();
        renderList();
      });
    }
    list.appendChild(item);
  }
}

function toggleTotpEditor(id) {
  if (editTotpId === id) {
    closeTotpEditor();
    renderList();
    return;
  }
  editTotpId = id;
  editTotpStatus = "";
  renderList();
}

async function saveTotpEdit(id) {
  const item = document.querySelector(`.cred-item[data-id="${String(id)}"]`);
  const input = item ? item.querySelector("[data-totp-input]") : null;
  const secret = input ? String(input.value || "").trim() : "";
  await safeCommand({ cmd: "update_totp", id, totp_secret: secret });
  editTotpStatus = secret ? "TOTP secret saved." : "TOTP secret removed.";
  await refresh();
  editTotpId = id;
  renderList();
}

function syncTotpPanel() {
  if (activeTotpId == null) return;
  const item = document.querySelector(`.cred-item[data-id="${String(activeTotpId)}"]`);
  if (!item) return;
  const codeEl = item.querySelector("[data-code]");
  const metaEl = item.querySelector("[data-meta]");
  if (codeEl) codeEl.textContent = totpDisplay.code || "------";
  if (metaEl) {
    metaEl.textContent = totpDisplay.meta || "";
    metaEl.classList.toggle("totp-error", Boolean(totpDisplay.error));
  }
}

async function updateActiveTotp() {
  const id = activeTotpId;
  if (id == null || !unlocked) return false;
  if (totpPending) return false;

  totpPending = true;
  try {
    const res = await safeCommand({ cmd: "get_totp", id });
    if (activeTotpId !== id) return false;
    setTotpDisplay({
      code: res.totp || "------",
      meta: `Expires in ${String(res.expires_in ?? "--")}s`,
      error: "",
    });
    syncTotpPanel();
    return true;
  } catch (e) {
    if (activeTotpId !== id) return false;
    const msg = (e && e.device && e.device.message) || e.message || "error";
    setTotpDisplay({
      code: "------",
      meta: describeTotpError(msg),
      error: msg,
    });
    syncTotpPanel();
    clearTotpTimer();
    return false;
  } finally {
    totpPending = false;
  }
}

async function openTotp(id) {
  clearTotpTimer();
  activeTotpId = id;
  setTotpDisplay({ code: "------", meta: "Loading TOTP...", error: "" });
  renderList();
  const ok = await updateActiveTotp();
  if (!ok || activeTotpId !== id) return;
  totpTimer = setInterval(() => {
    updateActiveTotp().catch(() => {});
  }, 1000);
}

function closeTotp() {
  stopTotpLoop();
  renderList();
}

async function toggleTotp(id) {
  if (activeTotpId === id) {
    closeTotp();
    return;
  }
  await openTotp(id);
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
  setStatus(`Connected in ${dt}ms.`);
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
el("btn-add-toggle").addEventListener("click", () => {
  const addForm = el("add-form");
  const btn = el("btn-add-toggle");
  addForm.classList.toggle("hidden");
  btn.textContent = addForm.classList.contains("hidden") ? "+ Add Credential" : "- Hide Form";
});
el("btn-clear-all").addEventListener("click", () => clearAllCredentials().catch((e) => setStatus(String(e && e.message ? e.message : e))));
el("btn-add").addEventListener("click", () => saveManual().catch((e) => setAddStatus(String(e && e.message ? e.message : e))));
el("btn-add-clear").addEventListener("click", clearAddForm);
el("add-password").addEventListener("keydown", (e) => {
  if (e.key === "Enter") saveManual().catch((err) => setAddStatus(String(err && err.message ? err.message : err)));
});
el("add-totp").addEventListener("keydown", (e) => {
  if (e.key === "Enter") saveManual().catch((err) => setAddStatus(String(err && err.message ? err.message : err)));
});

await loadSettings();
