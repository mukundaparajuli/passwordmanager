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
  const scale = 4;

  // SVG handling
  if (blob.type === "image/svg+xml") {
    const svgText = await blob.text();

    const svgBlob = new Blob([svgText], {
      type: "image/svg+xml"
    });

    const url = URL.createObjectURL(svgBlob);

    try {
      const img = new Image();

      await new Promise((resolve, reject) => {
        img.onload = resolve;
        img.onerror = reject;
        img.src = url;
      });

      const width = img.naturalWidth || img.width || 512;
      const height = img.naturalHeight || img.height || 512;

      const canvas = document.createElement("canvas");

      canvas.width = width * scale;
      canvas.height = height * scale;

      const ctx = canvas.getContext("2d", {
        willReadFrequently: true
      });

      // White background helps dark mode SVG QR codes
      ctx.fillStyle = "white";
      ctx.fillRect(0, 0, canvas.width, canvas.height);

      ctx.imageSmoothingEnabled = false;

      ctx.drawImage(
        img,
        0,
        0,
        canvas.width,
        canvas.height
      );

      return ctx.getImageData(
        0,
        0,
        canvas.width,
        canvas.height
      );
    } finally {
      URL.revokeObjectURL(url);
    }
  }

  // Normal PNG/JPEG/etc
  const bmp = await createImageBitmap(blob);

  try {
    const canvas = document.createElement("canvas");

    canvas.width = bmp.width * scale;
    canvas.height = bmp.height * scale;

    const ctx = canvas.getContext("2d", {
      willReadFrequently: true
    });

    // White background helps inverted QR codes
    ctx.fillStyle = "white";
    ctx.fillRect(0, 0, canvas.width, canvas.height);

    ctx.imageSmoothingEnabled = false;

    ctx.drawImage(
      bmp,
      0,
      0,
      canvas.width,
      canvas.height
    );

    return ctx.getImageData(
      0,
      0,
      canvas.width,
      canvas.height
    );
  } finally {
    bmp.close();
  }
}

async function decodeQrFromDataUrl(dataUrl) {
  try {
    const response = await fetch(dataUrl);

    const blob = await response.blob();

    console.log("QR blob type:", blob.type);

    // Try native BarcodeDetector first
    if ("BarcodeDetector" in window) {
      try {
        let detectBlob = blob;

        // SVGs need rasterization first
        if (blob.type === "image/svg+xml") {
          const imageData = await blobToImageData(blob);

          const canvas = document.createElement("canvas");

          canvas.width = imageData.width;
          canvas.height = imageData.height;

          const ctx = canvas.getContext("2d");

          ctx.putImageData(imageData, 0, 0);

          const rasterBlob = await new Promise((resolve) => {
            canvas.toBlob(resolve, "image/png");
          });

          detectBlob = rasterBlob;
        }

        const detector = new BarcodeDetector({
          formats: ["qr_code"]
        });

        const bitmap = await createImageBitmap(detectBlob);

        try {
          const codes = await detector.detect(bitmap);

          if (
            codes &&
            codes.length > 0 &&
            codes[0].rawValue
          ) {
            console.log("BarcodeDetector success");

            return codes[0].rawValue;
          }
        } finally {
          bitmap.close();
        }
      } catch (err) {
        console.warn("BarcodeDetector failed:", err);
      }
    }

    // Fallback to jsQR
    const jsQRFn = globalThis.jsQR;

    if (typeof jsQRFn === "function") {
      const imageData = await blobToImageData(blob);

      console.log(
        "Trying jsQR:",
        imageData.width,
        imageData.height
      );

      const result = jsQRFn(
        imageData.data,
        imageData.width,
        imageData.height,
        {
          inversionAttempts: "attemptBoth"
        }
      );

      if (result?.data) {
        console.log("jsQR success");

        return result.data;
      }
    }

    console.warn("QR decode failed");

    return null;
  } catch (err) {
    console.error("decodeQrFromDataUrl error:", err);

    return null;
  }
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
  const manualTweak = el("manual-tweak").value.trim();
  
  // Populate manual-tweak field first if QR was decoded
  if (lastDecodedRaw && !manualTweak) {
    el("manual-tweak").value = lastDecodedRaw;
  }

  // Try sources in order of priority: manual paste → manual tweak → QR decoded
  let source = manualPre || manualTweak || lastDecodedRaw;
  let parsed = parseOtpauth(source);
  
  // If parsing failed, try alternatives
  if (parsed.error) {
    if (manualTweak && manualTweak !== source) {
      parsed = parseOtpauth(manualTweak);
    }
    if (parsed.error && lastDecodedRaw && lastDecodedRaw !== source) {
      parsed = parseOtpauth(lastDecodedRaw);
    }
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

  const idStr = el("field-cred").value;
  const id = parseInt(idStr, 10);
  const toSave = el("field-secret").value.trim().replace(/\s/g, "").toUpperCase();

  if (idStr === "" || isNaN(id) || !toSave) {
    setStatus("Pick a credential and ensure the secret is not empty.");
    return;
  }

  if (!/^[A-Z2-7]+=*$/.test(toSave) || toSave.length < 8) {
    setStatus("Secret doesn't look like valid Base32. Check the secret field.");
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
