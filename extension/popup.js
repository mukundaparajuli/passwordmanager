import { VaultKeySerial } from "./serial.js";

// Icons is loaded globally by icons.js in manifest
// Access it as window.Icons in the code

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
  totpDisplay: { code: "------", meta: "Open TOTP to load a code.", error: "" },
  openMenuId: null,
  initializationStarted: false
};

let autofillBannerDismissed = false;

function domainLookupKeys(hostname) {
  const h = String(hostname || "")
    .trim()
    .toLowerCase();
  if (!h) return [];
  const keys = [h];
  if (h.startsWith("www.")) keys.push(h.slice(4));
  else keys.push(`www.${h}`);
  return keys;
}

function domainMapEntryForHost(hostname, domainMap) {
  if (!domainMap || typeof domainMap !== "object") return null;
  for (const k of domainLookupKeys(hostname)) {
    const row = domainMap[k];
    if (row && typeof row === "object") return row;
  }
  return null;
}

function credHostVariants(credHost) {
  const h = String(credHost || "").toLowerCase();
  if (!h) return [];
  const s = new Set([h]);
  if (h.startsWith("www.")) s.add(h.slice(4));
  else s.add(`www.${h}`);
  return [...s];
}

function credMatchesPageDomain(c, pageDomain) {
  if (!pageDomain) return false;
  const credHost = hostFromUrl(c.url || "").toLowerCase();
  if (!credHost) return false;
  const targets = new Set(domainLookupKeys(pageDomain));
  return credHostVariants(credHost).some((h) => targets.has(h));
}

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
  console.log("[VaultKey-Popup] setView called with:", which);
  const dv = el("view-disconnected");
  const lv = el("view-locked");
  const uv = el("view-unlocked");
  console.log("[VaultKey-Popup] Elements found:", { dv: !!dv, lv: !!lv, uv: !!uv });
  
  dv.classList.toggle("hidden", which !== "disconnected");
  lv.classList.toggle("hidden", which !== "locked");
  uv.classList.toggle("hidden", which !== "unlocked");
  
  console.log("[VaultKey-Popup] After setView - classes:", {
    disconnected: dv.className,
    locked: lv.className,
    unlocked: uv.className
  });
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

async function initializePopup() {
  // Prevent double initialization
  if (state.initializationStarted) {
    console.log("[VaultKey-Popup] Initialization already started, skipping");
    return;
  }
  state.initializationStarted = true;
  
  // Check if device was recently unlocked (use local storage for persistence across popup instances)
  const { deviceUnlockedAt: savedTime, deviceUnlockedExpiry } = await chrome.storage.local.get({ deviceUnlockedAt: 0, deviceUnlockedExpiry: 0 });
  const now = Date.now();
  
  console.log("[VaultKey-Popup] Init check - savedTime:", savedTime, "expiry:", deviceUnlockedExpiry, "now:", now);
  
  // Clean up expired unlock
  if (deviceUnlockedExpiry && now > deviceUnlockedExpiry) {
    console.log("[VaultKey-Popup] Unlock expired, clearing");
    await chrome.storage.local.set({ deviceUnlockedAt: 0, deviceUnlockedExpiry: 0 });
  } else if (savedTime && now <= deviceUnlockedExpiry) {
    console.log("[VaultKey-Popup] Unlock still valid");
  }
  
  if (savedTime && deviceUnlockedExpiry && now <= deviceUnlockedExpiry) {
    console.log("[VaultKey-Popup] Device was recently unlocked, trying to auto-connect");
    // Try to connect and show credentials without asking for PIN
    try {
      state.serial = new VaultKeySerial();
      state.serial.onDisconnect(() => {
        console.log("[VaultKey-Popup] Device disconnected! (onDisconnect callback fired)");
        stopTotpLoop();
        closeTotpEditor();
        state.serial = null;
        state.unlocked = false;
        state.creds = [];
        chrome.storage.local.set({ deviceUnlockedAt: 0, deviceUnlockedExpiry: 0 });
        setView("disconnected");
        setStatus("Device disconnected.");
      });

      // Auto-connect with previously selected port
      console.log("[VaultKey-Popup] Attempting auto-connect...");
      await state.serial.connect({ auto: true });
      console.log("[VaultKey-Popup] Connected successfully");

      // Try to fetch credentials
      try {
        const res = await safeCommand({ cmd: "list" });
        state.creds = Array.isArray(res.credentials) ? res.credentials : [];
        state.unlocked = true;
        console.log("[VaultKey-Popup] Device unlocked! Credentials:", state.creds.length);
        
        renderList();
        setView("unlocked");
        setStatus("Connected and unlocked.");
        
        try {
          await updateDomainIndicator();
        } catch (domainErr) {
          console.error("[VaultKey-Popup] updateDomainIndicator failed:", domainErr);
        }
        console.log("[VaultKey-Popup] Initialization complete (from session)!");
        return;
      } catch (e) {
        // Device is locked or timeout, fall through to normal connect flow
        console.log("[VaultKey-Popup] Device not responding or locked:", e.message);
        setView("locked");
        setStatus("Connected. Enter PIN to unlock.");
        return;
      }
    } catch (e) {
      console.log("[VaultKey-Popup] Could not auto-connect:", e.message);
      // Fall through to normal flow
    }
  } else {
    console.log("[VaultKey-Popup] No valid unlock found - showing connect button");
  }
  
  // Normal initialization flow - show connect button
  console.log("[VaultKey-Popup] Starting normal initialization");
  setView("disconnected");
}

async function connect() {
  setStatus("");
  console.log("[VaultKey-Popup] connect() called");
  // If already connected, just show locked view
  if (state.serial) {
    console.log("[VaultKey-Popup] Already connected");
    setView("locked");
    setStatus("Already connected.");
    return;
  }
  state.serial = new VaultKeySerial();
  state.serial.onDisconnect(() => {
    console.log("[VaultKey-Popup] Device disconnected from connect() handler");
    stopTotpLoop();
    closeTotpEditor();
    state.serial = null;
    state.unlocked = false;
    state.creds = [];
    setView("disconnected");
    setStatus("Disconnected.");
  });
  try {
    console.log("[VaultKey-Popup] Attempting to connect...");
    await state.serial.connect({ auto: true });
    console.log("[VaultKey-Popup] Connected successfully");
  } catch (e) {
    console.error("[VaultKey-Popup] Connect error:", e.message);
    // requestPort fails in popup context — guide user to Options page
    if (e && e.message && e.message.includes("No port selected")) {
      state.serial = null;
      setStatus("No device selected yet. Open Settings once, choose the port, then return here.");
      return;
    }
    // Clean up on connection error
    state.serial = null;
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
  await chrome.storage.local.set({ deviceUnlockedAt: 0, deviceUnlockedExpiry: 0 });
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
  
  // Remember that device was unlocked (30 min timeout)
  const now = Date.now();
  const expiry = now + 30 * 60 * 1000; // 30 minutes from now
  await chrome.storage.local.set({ deviceUnlockedAt: now, deviceUnlockedExpiry: expiry });
  console.log("[VaultKey-Popup] Device unlocked, saved until:", new Date(expiry));
  
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
    if (!h) continue;
    const entry = { id: c.id, service: c.service || "", username: c.username || "" };
    domainMap[h] = entry;
    const alt = h.startsWith("www.") ? h.slice(4) : `www.${h}`;
    domainMap[alt] = entry;
  }
  await chrome.storage.local.set({ domainMap });

  // Notify all tabs that domainMap was updated so content scripts can re-check
  const tabs = await chrome.tabs.query({});
  for (const tab of tabs) {
    try {
      chrome.tabs.sendMessage(tab.id, { type: "DOMAIN_MAP_UPDATED" });
    } catch (e) {
      // Tab might not have content script loaded, ignore
    }
  }

  await updateDomainIndicator();
}

async function updateDomainIndicator() {
  const domain = await getActiveDomain();
  if (!domain) {
    el("domain-indicator").textContent = "";
    return;
  }
  const { domainMap } = await chrome.storage.local.get({ domainMap: {} });
  const row = domainMapEntryForHost(domain, domainMap);
  if (row) {
    el("domain-indicator").textContent = row.service
      ? `Saved login · ${row.service}`
      : `Saved login for ${domain}`;
  } else {
    el("domain-indicator").textContent = `No saved login for ${domain}`;
  }
}

function syncAutofillBanner() {
  const banner = el("autofill-banner");
  const titleEl = el("autofill-banner-title");
  const subEl = el("autofill-banner-sub");
  if (!banner || !titleEl || !subEl) return;

  if (state.unlocked) {
    el("search").placeholder =
      state.mode === "autofill" && state.domain ? "Type to search all sites…" : "Search";
  }

  if (state.mode !== "autofill" || !state.domain || !state.unlocked) {
    banner.classList.add("hidden");
    return;
  }
  if (autofillBannerDismissed) {
    banner.classList.add("hidden");
    return;
  }

  const hasMatch = state.creds.some((c) => credMatchesPageDomain(c, state.domain));
  if (hasMatch) {
    titleEl.textContent = "Autofill this site";
    subEl.textContent =
      "Tap Autofill on the highlighted login, then confirm twice on your device to type username and password.";
  } else {
    titleEl.textContent = "No saved login for this tab";
    subEl.textContent =
      "Sign in once, then save from this extension, or add the site manually under Add.";
  }
  banner.classList.remove("hidden");
}

function renderList() {
  const q = (el("search").value || "").trim().toLowerCase();
  const searchEmpty = !q;
  const list = el("list");
  list.innerHTML = "";

  let pool = state.creds;
  if (state.mode === "autofill" && state.domain && searchEmpty) {
    pool = state.creds.filter((c) => credMatchesPageDomain(c, state.domain));
  }

  const filtered = pool.filter((c) => {
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
    if (state.mode === "autofill" && state.domain && searchEmpty && state.creds.length > 0) {
      list.innerHTML =
        '<div class="muted">Nothing saved for this site yet. Use Search to pick another login, or add this site with Add.</div>';
    } else {
      list.innerHTML = '<div class="muted">No credentials found.</div>';
    }
    syncAutofillBanner();
    return;
  }

  for (const c of filtered) {
    const totpOpen = state.activeTotpId === c.id;
    const editOpen = state.editTotpId === c.id;
    const metaClass = state.totpDisplay.error ? "totp-meta totp-error" : "totp-meta";
    const menuOpen = state.openMenuId === c.id;
    const isAutofillMatch = state.mode === "autofill" && state.domain && credMatchesPageDomain(c, state.domain);
    const item = document.createElement("div");
    item.className = isAutofillMatch ? "item item-autofill-match" : "item";
    item.dataset.id = String(c.id);
    
    // Main item content with minimalistic design
    const itemHeader = document.createElement("div");
    itemHeader.className = "item-header";
    
    const itemInfo = document.createElement("div");
    itemInfo.className = "item-info";
    itemInfo.innerHTML = `
      <div class="svc">${escapeHtml(c.service || "(unnamed)")}</div>
      <div class="url">${escapeHtml(c.url || "")}</div>
    `;
    itemHeader.appendChild(itemInfo);
    
    // Menu dropdown (hidden by default)
    const menuDropdown = document.createElement("div");
    menuDropdown.className = "menu-dropdown";
    if (!menuOpen) menuDropdown.style.display = "none";
    
    const selectMenuItem = document.createElement("button");
    selectMenuItem.className = "menu-item";
    selectMenuItem.setAttribute("data-act", "select");
    selectMenuItem.textContent = isAutofillMatch ? "Autofill" : "Select";
    menuDropdown.appendChild(selectMenuItem);
    
    const totpMenuItem = document.createElement("button");
    totpMenuItem.className = "menu-item";
    totpMenuItem.setAttribute("data-act", "totp");
    totpMenuItem.textContent = totpOpen ? "Hide TOTP" : "Show TOTP";
    menuDropdown.appendChild(totpMenuItem);
    
    const addTotpMenuItem = document.createElement("button");
    addTotpMenuItem.className = "menu-item";
    addTotpMenuItem.setAttribute("data-act", "edit-totp");
    addTotpMenuItem.textContent = c.has_totp ? (editOpen ? "Cancel TOTP" : "Replace TOTP") : (editOpen ? "Cancel TOTP" : "Add TOTP");
    menuDropdown.appendChild(addTotpMenuItem);

    const menuContainer = document.createElement("div");
    menuContainer.className = "menu-container";
    
    const menuButton = document.createElement("button");
    menuButton.className = "menu-button";
    menuButton.setAttribute("data-act", "toggle-menu");
    menuButton.setAttribute("title", "More options");
    menuButton.setAttribute("aria-label", "More options");
    
    // Menu icon - try to use icon utility, fallback to SVG
    let menuIcon = null;
    try {
      if (window.Icons && window.Icons.createIconSync) {
        menuIcon = window.Icons.createIconSync("menu", {
          width: "20",
          height: "20",
          style: "pointer-events: none;"
        });
      }
    } catch (e) {
      console.error("[VaultKey-Popup] Failed to create menu icon:", e);
    }
    
    // If icon creation failed, use SVG fallback with vertical dots
    if (!menuIcon) {
      menuIcon = document.createElementNS("http://www.w3.org/2000/svg", "svg");
      menuIcon.setAttribute("width", "20");
      menuIcon.setAttribute("height", "20");
      menuIcon.setAttribute("viewBox", "0 0 20 20");
      menuIcon.setAttribute("fill", "currentColor");
      menuIcon.style.pointerEvents = "none";
      
      // Create three dots vertically
      const circle1 = document.createElementNS("http://www.w3.org/2000/svg", "circle");
      circle1.setAttribute("cx", "10");
      circle1.setAttribute("cy", "4");
      circle1.setAttribute("r", "2");
      
      const circle2 = document.createElementNS("http://www.w3.org/2000/svg", "circle");
      circle2.setAttribute("cx", "10");
      circle2.setAttribute("cy", "10");
      circle2.setAttribute("r", "2");
      
      const circle3 = document.createElementNS("http://www.w3.org/2000/svg", "circle");
      circle3.setAttribute("cx", "10");
      circle3.setAttribute("cy", "16");
      circle3.setAttribute("r", "2");
      
      menuIcon.appendChild(circle1);
      menuIcon.appendChild(circle2);
      menuIcon.appendChild(circle3);
    }
    
    if (menuIcon) menuButton.appendChild(menuIcon);
    menuContainer.appendChild(menuButton);
    menuContainer.appendChild(menuDropdown);
    
    itemHeader.appendChild(menuContainer);
    item.appendChild(itemHeader);

    // TOTP panel
    if (totpOpen) {
      const totpPanel = document.createElement("div");
      totpPanel.className = "totp-panel";
      totpPanel.dataset.totpPanel = "";
      totpPanel.innerHTML = `
        <div class="totp-code" data-code>${escapeHtml(state.totpDisplay.code || "------")}</div>
        <div class="${metaClass}" data-meta>${escapeHtml(state.totpDisplay.meta || "Open TOTP to load a code.")}</div>
      `;
      item.appendChild(totpPanel);
    }

    // TOTP edit panel
    if (editOpen) {
      const editPanel = document.createElement("div");
      editPanel.className = "totp-panel";
      editPanel.dataset.totpEditPanel = "";
      editPanel.innerHTML = `
        <div class="totp-meta">Enter a new Base32 secret. Save blank to remove the stored TOTP secret.</div>
        <div class="row" style="margin-top: 8px">
          <input data-totp-input type="text" placeholder="BASE32SECRET" style="flex: 1" />
        </div>
        <div class="row" style="margin-top: 8px">
          <button data-act="save-totp" class="primary">Save TOTP</button>
          <button data-act="cancel-totp-edit">Cancel</button>
        </div>
        <div class="totp-meta${state.editTotpStatus ? "" : " hidden"}" data-totp-edit-status>${escapeHtml(state.editTotpStatus || "")}</div>
      `;
      item.appendChild(editPanel);
    }

    // Attach event listeners
    const toggleMenuBtn = itemHeader.querySelector('[data-act="toggle-menu"]');
    toggleMenuBtn.addEventListener("click", (e) => {
      e.preventDefault();
      e.stopPropagation();
      state.openMenuId = state.openMenuId === c.id ? null : c.id;
      renderList();
    });

    const selectMenuBtn = menuDropdown.querySelector('[data-act="select"]');
    selectMenuBtn.addEventListener("click", async (e) => {
      e.preventDefault();
      e.stopPropagation();
      state.openMenuId = null;
      await safeCommand({ cmd: "select", id: c.id });
      setStatus(`Selected ${c.service}. Confirm twice on the device to type.`);
    });

    const totpMenuBtn = menuDropdown.querySelector('[data-act="totp"]');
    totpMenuBtn.addEventListener("click", (e) => {
      e.preventDefault();
      e.stopPropagation();
      state.openMenuId = null;
      toggleTotp(c.id).catch((err) => {
        setStatus(String(err && err.message ? err.message : err));
      });
    });

    const editTotpMenuBtn = menuDropdown.querySelector('[data-act="edit-totp"]');
    editTotpMenuBtn.addEventListener("click", (e) => {
      e.preventDefault();
      e.stopPropagation();
      state.openMenuId = null;
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

  syncAutofillBanner();
  const firstMatch = list.querySelector(".item-autofill-match");
  if (firstMatch && state.mode === "autofill" && state.domain) {
    requestAnimationFrame(() => firstMatch.scrollIntoView({ block: "nearest", behavior: "smooth" }));
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
await initializePopup();
el("search").addEventListener("input", renderList);
const autofillDismissBtn = el("autofill-banner-dismiss");
if (autofillDismissBtn) {
  autofillDismissBtn.addEventListener("click", () => {
    autofillBannerDismissed = true;
    syncAutofillBanner();
  });
}

// Close menu when clicking outside
document.addEventListener("click", (e) => {
  if (state.openMenuId != null && !e.target.closest(".menu-button") && !e.target.closest(".menu-dropdown")) {
    state.openMenuId = null;
    renderList();
  }
});

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
  await chrome.storage.local.set({ deviceUnlockedAt: 0, deviceUnlockedExpiry: 0 });
  setView("locked");
  setStatus("Locked.");
});
el("btn-clear-captured").addEventListener("click", clearCaptured);
el("btn-save").addEventListener("click", saveCaptured);
el("btn-show-add").addEventListener("click", showAddForm);
el("btn-add-cancel").addEventListener("click", hideAddForm);
el("btn-add-save").addEventListener("click", () => saveManual().catch(e => setStatus(String(e && e.message ? e.message : e))));

// Bootstrap (view already set by initializePopup — do not reset it here)
await loadCaptured();
await updateDomainIndicator();
