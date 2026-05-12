const ICON_DATA_URL =
  "data:image/png;base64,iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAQAAAC1HAwCAAAAC0lEQVR42mP8/x8AAwMB/6XGZ0cAAAAASUVORK5CYII=";

const NOTIF_ID = "vaultkey_save";
const CTX_MENU_QR_TOTP = "vaultkey-save-totp-qr";

function arrayBufferToBase64(buffer) {
  const bytes = new Uint8Array(buffer);
  let binary = "";
  const chunk = 0x8000;
  for (let i = 0; i < bytes.length; i += chunk) {
    binary += String.fromCharCode.apply(null, bytes.subarray(i, Math.min(i + chunk, bytes.length)));
  }
  return btoa(binary);
}

async function fetchImageAsDataUrl(srcUrl) {
  const r = await fetch(srcUrl);
  if (!r.ok) throw new Error(`HTTP ${r.status}`);
  const buf = await r.arrayBuffer();
  const ct = r.headers.get("content-type") || "image/png";
  return `data:${ct};base64,${arrayBufferToBase64(buf)}`;
}

async function blobUrlToDataUrlInTab(tabId, srcUrl) {
  const res = await chrome.scripting.executeScript({
    target: { tabId },
    func: (u) =>
      fetch(u)
        .then((r) => r.blob())
        .then(
          (blob) =>
            new Promise((resolve, reject) => {
              const fr = new FileReader();
              fr.onload = () => resolve(fr.result);
              fr.onerror = () => reject(new Error("read failed"));
              fr.readAsDataURL(blob);
            })
        ),
    args: [srcUrl]
  });
  const result = res && res[0] && res[0].result;
  if (!result || typeof result !== "string") throw new Error("Could not read image from page.");
  return result;
}

async function loadQrImageFromContext(info, tab) {
  const srcUrl = info.srcUrl;
  if (!srcUrl) throw new Error("No image URL.");
  try {
    return await fetchImageAsDataUrl(srcUrl);
  } catch (e) {
    if (tab && tab.id != null && String(srcUrl).startsWith("blob:")) {
      try {
        return await blobUrlToDataUrlInTab(tab.id, srcUrl);
      } catch (_) {
        throw e;
      }
    }
    throw e;
  }
}

function registerContextMenus() {
  chrome.contextMenus.removeAll(() => {
    chrome.contextMenus.create({
      id: CTX_MENU_QR_TOTP,
      title: "Save QR as TOTP secret…",
      contexts: ["image"]
    });
  });
}

chrome.runtime.onInstalled.addListener(() => registerContextMenus());
registerContextMenus();

chrome.contextMenus.onClicked.addListener((info, tab) => {
  if (info.menuItemId !== CTX_MENU_QR_TOTP) return;
  (async () => {
    try {
      const dataUrl = await loadQrImageFromContext(info, tab);
      await chrome.storage.session.set({ totpQrDataUrl: dataUrl, totpQrError: null });
    } catch (e) {
      const msg = String(e && e.message ? e.message : e);
      await chrome.storage.session.set({ totpQrDataUrl: null, totpQrError: msg });
    }
    openExtensionPopupWindow("totp-qr.html");
  })();
});

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

function pickDomainMapEntry(domain, domainMap) {
  if (!domainMap || typeof domainMap !== "object") return null;
  for (const k of domainLookupKeys(domain)) {
    const hit = domainMap[k];
    if (hit && typeof hit === "object") return { matchedKey: k, ...hit };
  }
  return null;
}

function openExtensionPopupWindow(urlPath) {
  const url = chrome.runtime.getURL(urlPath || "popup.html");
  chrome.tabs.create({ url, active: true }, () => {
    if (chrome.runtime.lastError) {
      chrome.windows.create({
        url,
        type: "popup",
        width: 420,
        height: 680,
        focused: true
      });
    }
  });
}

chrome.runtime.onMessage.addListener((msg, sender, sendResponse) => {
  if (msg && typeof msg === "object" && msg.type === "OPEN_POPUP") {
    try {
      openExtensionPopupWindow(msg.urlPath || "popup.html");
      sendResponse({ ok: true });
    } catch (e) {
      sendResponse({ ok: false, error: String(e && e.message ? e.message : e) });
    }
    return false;
  }

  (async () => {
    if (!msg || typeof msg !== "object" || !msg.type) return;

    if (msg.type === "CREDENTIALS_CAPTURED") {
      const payload = {
        service: msg.service || "",
        url: msg.url || "",
        username: msg.username || "",
        password: msg.password || ""
      };

      await chrome.storage.session.set({ captured: payload });

      await chrome.notifications.create(NOTIF_ID, {
        type: "basic",
        iconUrl: ICON_DATA_URL,
        title: "Save login",
        message: "Review and save this password.",
        buttons: [{ title: "Open" }]
      });

      sendResponse({ ok: true });
      return;
    }

    if (msg.type === "CHECK_DOMAIN") {
      const domain = String(msg.domain || "");
      console.log("[VaultKey-BG] CHECK_DOMAIN request for:", domain);
      const { domainMap } = await chrome.storage.local.get({ domainMap: {} });
      const hit = pickDomainMapEntry(domain, domainMap);
      console.log("[VaultKey-BG] Found credentials for domain:", Boolean(hit));
      sendResponse({
        exists: Boolean(hit),
        id: hit ? hit.id : null,
        service: hit ? hit.service || "" : "",
        username: hit ? hit.username || "" : "",
        matchedKey: hit ? hit.matchedKey || "" : ""
      });
      return;
    }
  })().catch((e) => {
    sendResponse({ ok: false, error: String(e && e.message ? e.message : e) });
  });

  return true;
});

chrome.notifications.onButtonClicked.addListener((id) => {
  if (id !== NOTIF_ID) return;
  openExtensionPopupWindow("popup.html?mode=save");
});

chrome.notifications.onClicked.addListener((id) => {
  if (id !== NOTIF_ID) return;
  openExtensionPopupWindow("popup.html?mode=save");
});
