const ICON_DATA_URL =
  "data:image/png;base64,iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAQAAAC1HAwCAAAAC0lEQVR42mP8/x8AAwMB/6XGZ0cAAAAASUVORK5CYII=";

const NOTIF_ID = "vaultkey_save";

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
