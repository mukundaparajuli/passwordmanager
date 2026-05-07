import { VaultKeySerial } from "./serial.js";

const el = (id) => document.getElementById(id);

const state = {
  serial: null,
  unlocked: false,
  creds: [],
  domain: "",
  mode: "",
  settings: { autoLockMs: 120000, defaultHidMode: 1 },
  activeTotpId: null,
  editTotpId: null,
  editTotpStatus: "",
  totpTimer: null,
  totpPending: false,
  totpDisplay: { code: "------", meta: "Open TOTP to load a code.", error: "" }
};

function clearTotpTimer() {
  if (state.totpTimer) clearInterval(state.totpTimer);
  state.totpTimer = null;
  state.totpPending = false;
}

function stopTotpLoop() {
  clearTotpTimer();
  state.activeTotpId = null;
  state.totpDisplay = { code: "------", meta: "Open TOTP to load a code.", error: "" };
}

function closeTotpEditor() {
  state.editTotpId = null;
  state.editTotpStatus = "";
}

function setTotpDisplay(next) {
  state.totpDisplay = {
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

function setView(which) {
  el("view-disconnected").classList.toggle("hidden", which !== "disconnected");
  el("view-locked").classList.toggle("hidden", which !== "locked");
  el("view-unlocked").classList.toggle("hidden", which !== "unlocked");
}

function parseQuery() {
  const u = new URL(location.href);
  state.mode = u.searchParams.get("mode") || "";
  state.domain = u.searchParams.get("domain") || "";
}

async function getActiveDomain() {
  if (state.domain) return state.domain;
  const tabs = await chrome.tabs.query({ active: true, currentWindow: true });
  const url = tabs && tabs[0] && tabs[0].url ? tabs[0].url : "";
  try {
    const h = new URL(url).hostname;
    return h || "";
  } catch {
    return "";
  }
}

async function loadSettings() {
  const { settings } = await chrome.storage.local.get({ settings: { autoLockMs: 120000, defaultHidMode: 1 } });
  state.settings = settings;
}

async function safeCommand(cmdObj) {
  if (!state.serial) throw new Error("not connected");
  const res = await state.serial.command(cmdObj, 3000);
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
  state.serial = new VaultKeySerial();
  state.serial.onDisconnect(() => {
    stopTotpLoop();
    closeTotpEditor();
    state.serial = null;
    state.unlocked = false;
    state.creds = [];
    setView("disconnected");
    setStatus("Disconnected.");
  });
  try {
    await state.serial.connect({ auto: true });
  } catch (e) {
    // requestPort fails in popup context — guide user to Options page
    if (e && e.message && e.message.includes("No port selected")) {
      state.serial = null;
      setStatus("No device selected yet. Open Settings once, choose the port, then return here.");
      return;
    }
    throw e;
  }
  await safeCommand({ cmd: "ping" });
  setView("locked");
  setStatus("Connected.");
}

async function disconnect() {
  if (!state.serial) return;
  stopTotpLoop();
  closeTotpEditor();
  await state.serial.disconnect();
  state.serial = null;
  state.unlocked = false;
  state.creds = [];
  setView("disconnected");
  setStatus("");
}

async function unlock() {
  setStatus("");
  const pin = el("pin").value.trim();
  await safeCommand({ cmd: "unlock", pin });
  try {
    await safeCommand({ cmd: "sync_time", timestamp: Math.floor(Date.now() / 1000) });
  } catch {}
  el("pin").value = "";
  state.unlocked = true;
  setView("unlocked");
  await refresh();
  setStatus("Unlocked.");
}

function hostFromUrl(s) {
  if (!s) return "";
  try {
    return new URL(s.includes("://") ? s : `https://${s}`).hostname;
  } catch {
    return "";
  }
}

async function refresh() {
  setStatus("");
  const res = await safeCommand({ cmd: "list" });
  state.creds = Array.isArray(res.credentials) ? res.credentials : [];
  if (state.activeTotpId != null && !state.creds.some((c) => c.id === state.activeTotpId)) {
    stopTotpLoop();
  }
  renderList();

  // Cache a domain->credential mapping for content script checks.
  const domainMap = {};
  for (const c of state.creds) {
    const h = hostFromUrl(c.url || "");
    if (h) domainMap[h] = { id: c.id, service: c.service || "", username: c.username || "" };
  }
  await chrome.storage.local.set({ domainMap });

  await updateDomainIndicator();
}

async function updateDomainIndicator() {
  const domain = await getActiveDomain();
  if (!domain) {
    el("domain-indicator").textContent = "";
    return;
  }
  const { domainMap } = await chrome.storage.local.get({ domainMap: {} });
  if (domainMap && domainMap[domain]) {
    el("domain-indicator").textContent = `Saved for ${domain}`;
  } else {
    el("domain-indicator").textContent = `No saved login for ${domain}`;
  }
}

function renderList() {
  const q = (el("search").value || "").trim().toLowerCase();
  const list = el("list");
  list.innerHTML = "";

  const filtered = state.creds.filter((c) => {
    if (!q) return true;
    return (
      (c.service || "").toLowerCase().includes(q) ||
      (c.url || "").toLowerCase().includes(q) ||
      (c.username || "").toLowerCase().includes(q)
    );
  });

  if (state.activeTotpId != null && !filtered.some((c) => c.id === state.activeTotpId)) {
    stopTotpLoop();
  }
  if (state.editTotpId != null && !filtered.some((c) => c.id === state.editTotpId)) {
    closeTotpEditor();
  }

  if (filtered.length === 0) {
    list.innerHTML = '<div class="muted">No credentials found.</div>';
    return;
  }

  for (const c of filtered) {
    const totpOpen = state.activeTotpId === c.id;
    const editOpen = state.editTotpId === c.id;
    const metaClass = state.totpDisplay.error ? "totp-meta totp-error" : "totp-meta";
    const item = document.createElement("div");
    item.className = "item";
    item.dataset.id = String(c.id);
    item.innerHTML = `
      <div class="row" style="justify-content: space-between">
        <div>
          <div class="svc">${escapeHtml(c.service || "(unnamed)")}</div>
          <div class="url">${escapeHtml(c.url || "")}</div>
        </div>
        <div class="badge">${c.id}</div>
      </div>
      <div class="muted" style="margin-top:6px">User: <span style="font-family: var(--mono)">${escapeHtml(c.username || "-")}</span></div>
      <div class="row item-actions">
        <button data-act="select" class="primary">Select</button>
        <button data-act="totp">${totpOpen ? "Hide TOTP" : "TOTP"}</button>
        <button data-act="edit-totp">${c.has_totp ? (editOpen ? "Cancel TOTP" : "Replace TOTP") : (editOpen ? "Cancel TOTP" : "Add TOTP")}</button>
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
          <div class="totp-meta${state.editTotpStatus ? "" : " hidden"}" data-totp-edit-status>${escapeHtml(state.editTotpStatus || "")}</div>
        </div>
      ` : ""}
      ${totpOpen ? `
        <div class="totp-panel" data-totp-panel>
          <div class="totp-code" data-code>${escapeHtml(state.totpDisplay.code || "------")}</div>
          <div class="${metaClass}" data-meta>${escapeHtml(state.totpDisplay.meta || "Open TOTP to load a code.")}</div>
        </div>
      ` : ""}
    `;
    item.querySelector('[data-act="select"]').addEventListener("click", async (e) => {
      e.preventDefault();
      e.stopPropagation();
      await safeCommand({ cmd: "select", id: c.id });
      setStatus(`Selected ${c.service}. Confirm twice on the device to type.`);
    });
    item.querySelector('[data-act="totp"]').addEventListener("click", (e) => {
      e.preventDefault();
      e.stopPropagation();
      toggleTotp(c.id).catch((err) => {
        setStatus(String(err && err.message ? err.message : err));
      });
    });
    item.querySelector('[data-act="edit-totp"]').addEventListener("click", (e) => {
      e.preventDefault();
      e.stopPropagation();
      toggleTotpEditor(c.id);
    });
    const saveBtn = item.querySelector('[data-act="save-totp"]');
    if (saveBtn) {
      saveBtn.addEventListener("click", (e) => {
        e.preventDefault();
        e.stopPropagation();
        saveTotpEdit(c.id).catch((err) => {
          state.editTotpStatus = String(err && err.message ? err.message : err);
          renderList();
        });
      });
    }
    const cancelBtn = item.querySelector('[data-act="cancel-totp-edit"]');
    if (cancelBtn) {
      cancelBtn.addEventListener("click", (e) => {
        e.preventDefault();
        e.stopPropagation();
        closeTotpEditor();
        renderList();
      });
    }
    list.appendChild(item);
  }
}

function toggleTotpEditor(id) {
  if (state.editTotpId === id) {
    closeTotpEditor();
    renderList();
    return;
  }
  state.editTotpId = id;
  state.editTotpStatus = "";
  renderList();
}

async function saveTotpEdit(id) {
  const item = document.querySelector(`.item[data-id="${String(id)}"]`);
  const input = item ? item.querySelector("[data-totp-input]") : null;
  const secret = input ? String(input.value || "").trim() : "";
  await safeCommand({ cmd: "update_totp", id, totp_secret: secret });
  state.editTotpStatus = secret ? "TOTP secret saved." : "TOTP secret removed.";
  await refresh();
  state.editTotpId = id;
  renderList();
}

function syncTotpPanel() {
  if (state.activeTotpId == null) return;
  const item = document.querySelector(`.item[data-id="${String(state.activeTotpId)}"]`);
  if (!item) return;
  const codeEl = item.querySelector("[data-code]");
  const metaEl = item.querySelector("[data-meta]");
  if (codeEl) codeEl.textContent = state.totpDisplay.code || "------";
  if (metaEl) {
    metaEl.textContent = state.totpDisplay.meta || "";
    metaEl.classList.toggle("totp-error", Boolean(state.totpDisplay.error));
  }
}

async function updateActiveTotp() {
  const id = state.activeTotpId;
  if (id == null || !state.unlocked) return false;
  if (state.totpPending) return false;

  state.totpPending = true;
  try {
    const res = await safeCommand({ cmd: "get_totp", id });
    if (state.activeTotpId !== id) return false;
    setTotpDisplay({
      code: res.totp || "------",
      meta: `Expires in ${String(res.expires_in ?? "--")}s`,
      error: "",
    });
    syncTotpPanel();
    return true;
  } catch (e) {
    if (state.activeTotpId !== id) return false;
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
    state.totpPending = false;
  }
}

async function openTotp(id) {
  clearTotpTimer();
  state.activeTotpId = id;
  setTotpDisplay({ code: "------", meta: "Loading TOTP...", error: "" });
  renderList();
  const ok = await updateActiveTotp();
  if (!ok || state.activeTotpId !== id) return;
  state.totpTimer = setInterval(() => {
    updateActiveTotp().catch(() => {});
  }, 1000);
}

function closeTotp() {
  stopTotpLoop();
  renderList();
}

async function toggleTotp(id) {
  if (state.activeTotpId === id) {
    closeTotp();
    return;
  }
  await openTotp(id);
}

function escapeHtml(s) {
  return String(s)
    .replaceAll("&", "&amp;")
    .replaceAll("<", "&lt;")
    .replaceAll(">", "&gt;")
    .replaceAll('"', "&quot;")
    .replaceAll("'", "&#039;");
}

async function loadCaptured() {
  const { captured } = await chrome.storage.session.get({ captured: null });
  if (!captured) {
    el("save-box").classList.add("hidden");
    return;
  }
  el("save-service").value = captured.service || "";
  el("save-url").value = captured.url || "";
  el("save-username").value = captured.username || "";
  el("save-password").value = captured.password || "";
  el("save-hid-mode").value = String(state.settings.defaultHidMode ?? 1);
  el("save-box").classList.remove("hidden");
}

async function clearCaptured() {
  await chrome.storage.session.remove(["captured"]);
  el("save-box").classList.add("hidden");
  setStatus("Cleared prompt.");
}

function showAddForm() {
  el("add-service").value = "";
  el("add-url").value = "";
  el("add-username").value = "";
  el("add-password").value = "";
  el("add-totp").value = "";
  el("add-hid-mode").value = String(state.settings.defaultHidMode ?? 1);
  el("add-box").classList.remove("hidden");
  el("add-service").focus();
}

function hideAddForm() {
  el("add-box").classList.add("hidden");
}

async function saveManual() {
  setStatus("");
  const service = el("add-service").value.trim();
  const password = el("add-password").value;
  if (!service) { setStatus("Service name is required."); return; }
  if (!password) { setStatus("Password is required."); return; }

  const payload = {
    cmd: "add",
    service,
    url: el("add-url").value.trim(),
    username: el("add-username").value.trim(),
    password,
    totp_secret: el("add-totp").value.trim(),
    hid_mode: Number(el("add-hid-mode").value)
  };
  await safeCommand(payload);
  hideAddForm();
  await refresh();
  setStatus(`Saved ${payload.service}`);
}

async function saveCaptured() {
  setStatus("");
  const payload = {
    cmd: "add",
    service: el("save-service").value.trim(),
    url: el("save-url").value.trim(),
    username: el("save-username").value.trim(),
    password: el("save-password").value,
    totp_secret: "",
    hid_mode: Number(el("save-hid-mode").value)
  };
  await safeCommand(payload);
  await clearCaptured();
  await refresh();
  setStatus(`Saved ${payload.service}`);
}

// Auto-lock (popup-side)
let lastInteraction = Date.now();
function touch() {
  lastInteraction = Date.now();
}
window.addEventListener("click", touch, { capture: true });
window.addEventListener("keydown", touch, { capture: true });
setInterval(async () => {
  if (!state.unlocked) return;
  const ms = state.settings.autoLockMs;
  if (!ms || ms <= 0) return;
  if (Date.now() - lastInteraction < ms) return;
  try {
    await safeCommand({ cmd: "lock" });
  } catch {}
  stopTotpLoop();
  closeTotpEditor();
  state.unlocked = false;
  setView("locked");
  setStatus("Auto-locked.");
}, 1000);

// Wiring
parseQuery();
await loadSettings();
el("search").addEventListener("input", renderList);
el("btn-connect").addEventListener("click", () => connect().catch(e => setStatus(String(e && e.message ? e.message : e))));
el("btn-disconnect").addEventListener("click", () => disconnect().catch(e => setStatus(String(e && e.message ? e.message : e))));
el("btn-unlock").addEventListener("click", () => unlock().catch(e => setStatus(String(e && e.message ? e.message : e))));
el("pin").addEventListener("keydown", (e) => {
  if (e.key === "Enter") unlock();
});
el("btn-refresh").addEventListener("click", refresh);
el("btn-lock").addEventListener("click", async () => {
  try {
    await safeCommand({ cmd: "lock" });
  } catch {}
  stopTotpLoop();
  closeTotpEditor();
  state.unlocked = false;
  setView("locked");
  setStatus("Locked.");
});
el("btn-clear-captured").addEventListener("click", clearCaptured);
el("btn-save").addEventListener("click", saveCaptured);
el("btn-show-add").addEventListener("click", showAddForm);
el("btn-add-cancel").addEventListener("click", hideAddForm);
el("btn-add-save").addEventListener("click", () => saveManual().catch(e => setStatus(String(e && e.message ? e.message : e))));

// Bootstrap
setView("disconnected");
await loadCaptured();
await updateDomainIndicator();

// If launched explicitly for save/autofill, still requires user to click Connect.
