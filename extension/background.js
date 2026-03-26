const ICON_DATA_URL =
  "data:image/png;base64,iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAQAAAC1HAwCAAAAC0lEQVR42mP8/x8AAwMB/6XGZ0cAAAAASUVORK5CYII=";

const NOTIF_ID = "vaultkey_save";

async function openPopup(urlPath) {
  const url = chrome.runtime.getURL(urlPath);
  await chrome.windows.create({
    url,
    type: "popup",
    width: 420,
    height: 680
  });
}

chrome.runtime.onMessage.addListener((msg, sender, sendResponse) => {
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
        title: "VaultKey",
        message: "Save this password to VaultKey?",
        buttons: [{ title: "Save" }]
      });

      sendResponse({ ok: true });
      return;
    }

    if (msg.type === "CHECK_DOMAIN") {
      const domain = String(msg.domain || "");
      const { domainMap } = await chrome.storage.local.get({ domainMap: {} });
      const hit = domainMap && domainMap[domain];
      sendResponse({ exists: Boolean(hit), id: hit ? hit.id : null });
      return;
    }

    if (msg.type === "OPEN_POPUP") {
      await openPopup(msg.urlPath || "popup.html");
      sendResponse({ ok: true });
      return;
    }
  })().catch((e) => {
    sendResponse({ ok: false, error: String(e && e.message ? e.message : e) });
  });

  return true;
});

chrome.notifications.onButtonClicked.addListener(async (id) => {
  if (id !== NOTIF_ID) return;
  await openPopup("popup.html?mode=save");
});

chrome.notifications.onClicked.addListener(async (id) => {
  if (id !== NOTIF_ID) return;
  await openPopup("popup.html?mode=save");
});

