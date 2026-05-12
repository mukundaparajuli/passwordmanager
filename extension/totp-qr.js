import { VaultKeySerial } from "./serial.js";

const el = (id) => document.getElementById(id);

let serial = null;
let creds = [];
let lastDecodedRaw = "";

function setStatus(s) {
  el("status").textContent = s || "";
}

function setView(which) {
  el("view-disconnected").classList.toggle("hidden", which !== "disconnected");
  el("view-locked").classList.toggle("hidden", which !== "locked");
  el("view-import").classList.toggle("hidden", which !== "import");
}

function hostFromUrl(s) {
  if (!s) return "";
  try {
    return new URL(s.includes("://") ? s : `https://${s}`).hostname;
  } catch {
    return "";
  }
}

async function refreshDomainMapFromCreds() {
  const domainMap = {};
  for (const c of creds) {
    const h = hostFromUrl(c.url || "");
    if (!h) continue;
    const entry = { id: c.id, service: c.service || "", username: c.username || "" };
    domainMap[h] = entry;
    const alt = h.startsWith("www.") ? h.slice(4) : `www.${h}`;
    domainMap[alt] = entry;
  }
  await chrome.storage.local.set({ domainMap });
  const tabs = await chrome.tabs.query({});
  for (const tab of tabs) {
    try {
      chrome.tabs.sendMessage(tab.id, { type: "DOMAIN_MAP_UPDATED" });
    } catch (_) {}
  }
}

function parseOtpauth(s) {
  const t = String(s || "").trim();
  if (!t) return { error: "Empty." };
  if (t.startsWith("otpauth://")) {
    try {
      const u = new URL(t);
      if (u.protocol !== "otpauth:") return { error: "Unsupported URL scheme." };
      if (u.hostname !== "totp") return { error: "Only TOTP (otpauth://totp/…) is supported." };
      const rawPath = decodeURIComponent((u.pathname || "/").replace(/^\//, ""));
      let issuer = u.searchParams.get("issuer") || "";
      let account = rawPath;
      const colon = rawPath.lastIndexOf(":");
      if (colon >= 0) {
        if (!issuer) issuer = rawPath.slice(0, colon);
        account = rawPath.slice(colon + 1);
      }
      const secret = (u.searchParams.get("secret") || "").replace(/\s/g, "");
      if (!secret) return { error: "No secret= in otpauth URL." };
      return { secret: secret.toUpperCase(), issuer, account };
    } catch {
      return { error: "Invalid otpauth URL." };
    }
  }
  const cleaned = t.replace(/\s/g, "").toUpperCase();
  if (/^[A-Z2-7]+=*$/.test(cleaned) && cleaned.length >= 8) return { secret: cleaned, issuer: "", account: "" };
  return { error: "Need otpauth://totp/… or a Base32 secret." };
}

function applyParsedToUi(parsed) {
  if (parsed.error) {
    el("decode-hint").textContent = parsed.error;
    el("decode-hint").classList.add("err");
  } else {
    el("field-secret").value = parsed.secret;
    el("decode-hint").textContent = "Secret ready. Pick a credential and save.";
    el("decode-hint").classList.remove("err");
  }
  const iss = [parsed.issuer, parsed.account].filter(Boolean).join(" · ");
  el("issuer-line").textContent = iss ? `Label: ${iss}` : "";
}

function reparseFromInputs() {
  const tweak = el("manual-tweak").value.trim();
  const pre = el("manual-paste").value.trim();
  const raw = tweak || pre || lastDecodedRaw;
  if (!raw) return;
  applyParsedToUi(parseOtpauth(raw));
}

async function blobToImageData(blob) {
  const bmp = await createImageBitmap(blob);
  const canvas = document.createElement("canvas");
  canvas.width = bmp.width;
  canvas.height = bmp.height;
  const ctx = canvas.getContext("2d", { willReadFrequently: true });
  ctx.drawImage(bmp, 0, 0);
  bmp.close();
  return ctx.getImageData(0, 0, canvas.width, canvas.height);
}

async function decodeQrFromDataUrl(dataUrl) {
  const r = await fetch(dataUrl);
  const blob = await r.blob();
  if ("BarcodeDetector" in window) {
    try {
      const d = new BarcodeDetector({ formats: ["qr_code"] });
      const bmp = await createImageBitmap(blob);
      const codes = await d.detect(bmp);
      bmp.close();
      if (codes && codes.length && codes[0].rawValue) return codes[0].rawValue;
    } catch (_) {}
  }
  const jsQR = globalThis.jsQR;
  if (typeof jsQR === "function") {
    const imageData = await blobToImageData(blob);
    const res = jsQR(imageData.data, imageData.width, imageData.height, { inversionAttempts: "attemptBoth" });
    if (res && res.data) return res.data;
  }
  return null;
}

function populateCredSelect() {
  const sel = el("field-cred");
  sel.innerHTML = "";
  if (creds.length === 0) {
    const o = document.createElement("option");
    o.value = "";
    o.textContent = "(No credentials — add one in the vault)";
    o.disabled = true;
    sel.appendChild(o);
    return;
  }
  for (const c of creds) {
    const o = document.createElement("option");
    o.value = String(c.id);
    const svc = c.service || "(unnamed)";
    const u = (c.username || "").trim();
    o.textContent = u ? `${svc} — ${u}` : svc;
    sel.appendChild(o);
  }
}

async function safeCommand(cmdObj) {
  if (!serial) throw new Error("not connected");
  const res = await serial.command(cmdObj, 8000);
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
  if (serial) return;
  serial = new VaultKeySerial();
  serial.onDisconnect(() => {
    serial = null;
    creds = [];
    el("pre-connect-extras").classList.remove("hidden");
    setView("disconnected");
    setStatus("Disconnected.");
  });
  try {
    await serial.connect({ auto: true });
  } catch (e) {
    serial = null;
    setStatus(String(e && e.message ? e.message : e));
    return;
  }
  await safeCommand({ cmd: "ping" });
  setView("locked");
  setStatus("Enter PIN.");
}

async function unlockAndImport() {
  setStatus("");
  const pin = el("pin").value.trim();
  await safeCommand({ cmd: "unlock", pin });
  try {
    await safeCommand({ cmd: "sync_time", timestamp: Math.floor(Date.now() / 1000) });
  } catch (_) {}
  el("pin").value = "";
  const res = await safeCommand({ cmd: "list" });
  creds = Array.isArray(res.credentials) ? res.credentials : [];
  populateCredSelect();
  el("pre-connect-extras").classList.add("hidden");
  await showImportView();
}

async function showImportView() {
  setView("import");
  const session = await chrome.storage.session.get({
    totpQrDataUrl: null,
    totpQrError: null
  });

  const dataUrl = session.totpQrDataUrl;
  const prev = el("qr-preview");
  if (dataUrl) {
    prev.src = dataUrl;
    prev.classList.remove("hidden");
    lastDecodedRaw = (await decodeQrFromDataUrl(dataUrl)) || "";
  } else {
    prev.removeAttribute("src");
    prev.classList.add("hidden");
    lastDecodedRaw = "";
  }

  const manualPre = el("manual-paste").value.trim();
  let source = manualPre || lastDecodedRaw;
  if (!source && el("manual-tweak").value.trim()) source = el("manual-tweak").value.trim();

  let parsed = parseOtpauth(source);
  if (parsed.error && lastDecodedRaw && lastDecodedRaw !== source) {
    parsed = parseOtpauth(lastDecodedRaw);
  }
  if (parsed.error && manualPre) {
    parsed = parseOtpauth(manualPre);
  }

  if (lastDecodedRaw && !el("manual-tweak").value.trim()) {
    el("manual-tweak").value = lastDecodedRaw;
  }

  applyParsedToUi(parsed);

  if (session.totpQrError) {
    el("decode-hint").textContent =
      `Could not download image (${String(session.totpQrError)}). ` +
      (parsed.error ? String(parsed.error) : "Use the secret field or paste otpauth below, then save.");
    el("decode-hint").classList.add("err");
  } else if (dataUrl && !lastDecodedRaw) {
    if (!parsed.error) {
      el("decode-hint").textContent = "No QR found in image; using pasted / parsed secret.";
      el("decode-hint").classList.remove("err");
    } else {
      el("decode-hint").textContent =
        "Could not read a QR from this image. Paste otpauth://… or Base32 in “Paste / fix” below.";
      el("decode-hint").classList.add("err");
    }
  } else if (!dataUrl && !parsed.error) {
    el("decode-hint").textContent = "Using pasted secret (no image).";
    el("decode-hint").classList.remove("err");
  } else if (dataUrl && lastDecodedRaw && !parsed.error) {
    el("decode-hint").textContent = "QR decoded. Confirm secret and pick a credential.";
    el("decode-hint").classList.remove("err");
  } else if (dataUrl && lastDecodedRaw && parsed.error) {
    el("decode-hint").textContent = "QR text is not valid TOTP. Fix in “Paste / fix” or edit Base32.";
    el("decode-hint").classList.add("err");
  }

  setStatus("");
}

async function saveTotp() {
  setStatus("");
  const id = parseInt(el("field-cred").value, 10);
  const secretField = el("field-secret").value.trim().replace(/\s/g, "").toUpperCase();
  const tweakRaw = el("manual-tweak").value.trim();
  const parsed = parseOtpauth(tweakRaw || secretField);
  const toSave = parsed.error ? secretField : parsed.secret || secretField;
  if (!id || !toSave) {
    setStatus("Pick a credential and ensure the secret is not empty.");
    return;
  }
  try {
    await safeCommand({ cmd: "update_totp", id, totp_secret: toSave });
    await refreshDomainMapFromCreds();
    await chrome.storage.session.remove(["totpQrDataUrl", "totpQrError", "totpQrSrcHint"]);
    setStatus("TOTP saved on device.");
  } catch (e) {
    setStatus(String(e && e.message ? e.message : e));
  }
}

async function boot() {
  const session = await chrome.storage.session.get({
    totpQrDataUrl: null,
    totpQrError: null
  });
  const msg = el("pre-connect-msg");
  if (session.totpQrError) {
    const err = String(session.totpQrError);
    msg.textContent = `Could not load image: ${err}. Paste otpauth://… or Base32 below.`;
  } else if (session.totpQrDataUrl) {
    msg.textContent = "QR image loaded. Connect your device, then unlock to decode and attach TOTP.";
  } else {
    msg.textContent =
      'Right-click a QR code image → "Save QR as TOTP secret…". Or paste otpauth://… / Base32 below.';
  }

  el("manual-paste").addEventListener("input", () => {
    if (!el("view-import").classList.contains("hidden")) reparseFromInputs();
  });
  el("manual-tweak").addEventListener("input", reparseFromInputs);
  el("field-secret").addEventListener("input", () => {
    el("decode-hint").classList.remove("err");
  });

  el("btn-connect").addEventListener("click", () => connect().catch((e) => setStatus(String(e && e.message ? e.message : e))));
  el("btn-unlock").addEventListener("click", () => unlockAndImport().catch((e) => setStatus(String(e && e.message ? e.message : e))));
  el("pin").addEventListener("keydown", (e) => {
    if (e.key === "Enter") unlockAndImport().catch((err) => setStatus(String(err && err.message ? err.message : err)));
  });
  el("btn-save").addEventListener("click", () => saveTotp().catch((e) => setStatus(String(e && e.message ? e.message : e))));
  el("btn-close").addEventListener("click", async () => {
    try {
      if (serial) await serial.disconnect();
    } catch (_) {}
    await chrome.storage.session.remove(["totpQrDataUrl", "totpQrError", "totpQrSrcHint"]);
    window.close();
  });

  setView("disconnected");
}

boot().catch((e) => {
  el("status").textContent = String(e && e.message ? e.message : e);
});
