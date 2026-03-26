(() => {
  const domain = location.hostname;

  function findPasswordField() {
    const fields = [...document.querySelectorAll('input[type="password"]')].filter((i) => !i.disabled && !i.readOnly);
    return fields[0] || null;
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

  function injectAutofillButton(passwordField) {
    const btn = document.createElement("button");
    btn.type = "button";
    btn.textContent = "Autofill with VaultKey";
    btn.style.cssText =
      "margin-top:8px;padding:6px 10px;border-radius:10px;border:1px solid rgba(0,0,0,0.25);background:#f2f5ff;color:#1b243a;font-size:12px;cursor:pointer;";
    btn.addEventListener("click", () => {
      chrome.runtime.sendMessage({ type: "OPEN_POPUP", urlPath: `popup.html?mode=autofill&domain=${encodeURIComponent(domain)}` });
    });
    passwordField.insertAdjacentElement("afterend", btn);
  }

  const pw = findPasswordField();
  if (!pw) return;

  chrome.runtime.sendMessage({ type: "CHECK_DOMAIN", domain }, (res) => {
    if (res && res.exists) injectAutofillButton(pw);
  });

  const form = pw.closest("form");
  if (!form) return;

  form.addEventListener(
    "submit",
    () => {
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
})();

