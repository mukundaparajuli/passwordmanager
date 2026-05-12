(() => {
  const domain = location.hostname;

  window.addEventListener(
    "click",
    (e) => {
      const t = e.target;
      if (!t || typeof t.closest !== "function") return;
      const card = t.closest(".pm-autofill-card");
      if (!card || !card.closest(".pm-autofill-host")) return;
      e.preventDefault();
      e.stopImmediatePropagation();
      const urlPath = `popup.html?mode=autofill&domain=${encodeURIComponent(domain)}`;
      const url = chrome.runtime.getURL(urlPath);
      let w = null;
      try {
        w = window.open(url, "vaultkey_autofill", "width=440,height=700,scrollbars=yes");
      } catch (_) {}
      if (w == null) {
        chrome.runtime.sendMessage({ type: "OPEN_POPUP", urlPath });
      }
    },
    true
  );

  const PM_STYLE_ID = "vaultkey-pm-autofill-style";
  const INJECTED = new WeakMap();

  function ensureStyles() {
    if (document.getElementById(PM_STYLE_ID)) return;
    const style = document.createElement("style");
    style.id = PM_STYLE_ID;
    style.textContent = `
      .pm-autofill-host {
        position: relative;
        z-index: 2147483640;
        margin-top: 6px;
        max-width: 100%;
        pointer-events: auto;
        font-family: ui-sans-serif, system-ui, -apple-system, Segoe UI, Roboto, sans-serif;
      }
      .pm-autofill-card {
        display: flex;
        align-items: center;
        gap: 6px;
        width: 100%;
        max-width: min(100%, 320px);
        padding: 5px 8px 5px 6px;
        border-radius: 8px;
        background: #faf9f6;
        border: 1px solid rgba(27, 29, 32, 0.12);
        box-shadow: 0 1px 3px rgba(17, 24, 39, 0.06);
        cursor: pointer;
        pointer-events: auto;
        transition: border-color 0.12s ease, background 0.12s ease;
      }
      .pm-autofill-card:hover {
        background: #f3f1ec;
        border-color: rgba(27, 29, 32, 0.2);
      }
      .pm-autofill-card:focus-visible {
        outline: 2px solid #1f2329;
        outline-offset: 1px;
      }
      .pm-autofill-line {
        flex: 1;
        min-width: 0;
        font-size: 12px;
        font-weight: 500;
        line-height: 1.2;
        color: #1b1d20;
        white-space: nowrap;
        overflow: hidden;
        text-overflow: ellipsis;
        text-align: left;
      }
      .pm-autofill-chevron {
        flex-shrink: 0;
        color: #9aa0a6;
        display: flex;
        align-items: center;
        opacity: 0.85;
      }
      .pm-autofill-icon-wrap {
        flex-shrink: 0;
        width: 26px;
        height: 26px;
        border-radius: 6px;
        background: rgba(31, 35, 41, 0.07);
        display: flex;
        align-items: center;
        justify-content: center;
      }
    `;
    (document.head || document.documentElement).appendChild(style);
  }

  function findUsernameField(form, passwordField) {
    const inputs = [...form.querySelectorAll("input")].filter((i) => i !== passwordField && !i.disabled && !i.readOnly);

    const candidates = inputs.filter((i) => {
      const t = (i.getAttribute("type") || "text").toLowerCase();
      return t === "text" || t === "email" || t === "tel";
    });

    const score = (i) => {
      const key = `${i.name || ""} ${i.id || ""} ${i.autocomplete || ""}`.toLowerCase();
      let s = 0;
      if (key.includes("user")) s += 4;
      if (key.includes("email")) s += 4;
      if (key.includes("login")) s += 3;
      if (key.includes("name")) s += 1;
      if (i.value && i.value.length) s += 1;
      return s;
    };

    candidates.sort((a, b) => score(b) - score(a));
    return candidates[0] || null;
  }

  function removeInjection(passwordField) {
    const row = INJECTED.get(passwordField);
    if (row && row.parentNode) row.parentNode.removeChild(row);
    INJECTED.delete(passwordField);
  }

  function injectAutofillRow(passwordField, meta) {
    ensureStyles();

    const service = String(meta.service || "").trim() || "Saved login";
    const user = String(meta.username || "").trim();
    const display = user ? `${service} · ${user}` : service;
    const tip = user ? `${service} — ${user}` : `${service}. Opens extension; confirm on device.`;
    const aria = `Autofill: ${tip}`;

    const existing = INJECTED.get(passwordField);
    if (
      existing &&
      existing.isConnected &&
      existing.classList.contains("pm-autofill-host") &&
      passwordField.nextElementSibling === existing
    ) {
      const card = existing.querySelector(".pm-autofill-card");
      const lineEl = card && card.querySelector(".pm-autofill-line");
      if (lineEl && lineEl.textContent === display && card.title === tip && card.getAttribute("aria-label") === aria) {
        return;
      }
      if (lineEl) lineEl.textContent = display;
      if (card) {
        card.title = tip;
        card.setAttribute("aria-label", aria);
      }
      return;
    }

    removeInjection(passwordField);

    const host = document.createElement("div");
    host.className = "pm-autofill-host";
    host.setAttribute("role", "region");

    const card = document.createElement("button");
    card.type = "button";
    card.className = "pm-autofill-card";
    card.title = tip;
    card.setAttribute("aria-label", aria);

    const iconWrap = document.createElement("div");
    iconWrap.className = "pm-autofill-icon-wrap";
    try {
      const keyIcon = Icons.createIconSync("key", { width: "14", height: "14", style: "color:#1f2329;" });
      if (keyIcon) iconWrap.appendChild(keyIcon);
    } catch (_) {}

    const lineEl = document.createElement("div");
    lineEl.className = "pm-autofill-line";
    lineEl.textContent = display;

    const chevWrap = document.createElement("span");
    chevWrap.className = "pm-autofill-chevron";
    try {
      const chev = Icons.createIconSync("chevron-right", { width: "14", height: "14", style: "pointer-events:none;" });
      if (chev) chevWrap.appendChild(chev);
    } catch (_) {}

    card.appendChild(iconWrap);
    card.appendChild(lineEl);
    card.appendChild(chevWrap);

    host.appendChild(card);
    passwordField.insertAdjacentElement("afterend", host);
    INJECTED.set(passwordField, host);
  }

  function visiblePasswordFields() {
    return [...document.querySelectorAll('input[type="password"]')].filter((el) => {
      if (el.disabled || el.readOnly) return false;
      const r = el.getBoundingClientRect();
      if (r.width < 2 || r.height < 2) return false;
      const st = window.getComputedStyle(el);
      if (st.visibility === "hidden" || st.display === "none") return false;
      return true;
    });
  }

  let debounceTimer = null;
  let scanSerial = Promise.resolve();

  function scheduleScan() {
    if (debounceTimer) clearTimeout(debounceTimer);
    debounceTimer = setTimeout(() => {
      scanSerial = scanSerial.then(() => scanAndInject()).catch(() => {});
    }, 200);
  }

  let domObserver = null;

  async function scanAndInject() {
    if (domObserver) domObserver.disconnect();
    try {
      const fields = visiblePasswordFields();
      if (fields.length === 0) {
        document.querySelectorAll(".pm-autofill-host").forEach((h) => h.remove());
        return;
      }

      const activeHosts = new Set();
      await Promise.all(
        fields.map(
          (pw) =>
            new Promise((resolve) => {
              chrome.runtime.sendMessage({ type: "CHECK_DOMAIN", domain }, (res) => {
                if (chrome.runtime.lastError) {
                  resolve();
                  return;
                }
                if (res && res.exists) {
                  injectAutofillRow(pw, { service: res.service || "", username: res.username || "" });
                  const host = INJECTED.get(pw);
                  if (host) activeHosts.add(host);
                } else {
                  removeInjection(pw);
                }
                resolve();
              });
            })
        )
      );

      document.querySelectorAll(".pm-autofill-host").forEach((h) => {
        if (!activeHosts.has(h)) h.remove();
      });
    } finally {
      if (domObserver) {
        domObserver.observe(document.documentElement, { childList: true, subtree: true });
      }
    }
  }

  document.addEventListener(
    "submit",
    (ev) => {
      const form = ev.target;
      if (!(form instanceof HTMLFormElement)) return;
      const pw = form.querySelector('input[type="password"]:not([disabled]):not([readonly])');
      if (!pw) return;
      const usernameField = findUsernameField(form, pw);
      const username = usernameField ? String(usernameField.value || "") : "";
      const password = String(pw.value || "");
      if (!password) return;

      chrome.runtime.sendMessage({
        type: "CREDENTIALS_CAPTURED",
        service: domain,
        url: domain,
        username,
        password
      });
    },
    true
  );

  domObserver = new MutationObserver(() => scheduleScan());
  domObserver.observe(document.documentElement, { childList: true, subtree: true });

  scanSerial = scanSerial.then(() => scanAndInject()).catch(() => {});

  chrome.runtime.onMessage.addListener((msg) => {
    if (msg && msg.type === "DOMAIN_MAP_UPDATED") scheduleScan();
  });
})();
