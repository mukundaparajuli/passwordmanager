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

  function injectPasswordSuggestion(passwordField) {
    // Create a suggestion container
    const container = document.createElement("div");
    container.className = "pm-suggestion-container";
    container.style.cssText = `
      display: flex;
      align-items: center;
      gap: 8px;
      margin-top: 8px;
      padding: 10px 12px;
      border-radius: 12px;
      background: linear-gradient(135deg, #f3f1ec 0%, #faf9f6 100%);
      border: 1px solid rgba(27, 29, 32, 0.08);
      box-shadow: 0 4px 14px rgba(17, 24, 39, 0.08);
      font-family: ui-sans-serif, system-ui, -apple-system, Segoe UI, Roboto, sans-serif;
      cursor: pointer;
      transition: all 0.2s ease;
    `;
    
    container.addEventListener("mouseenter", () => {
      container.style.background = "linear-gradient(135deg, #eceae4 0%, #f5f4f0 100%)";
      container.style.boxShadow = "0 6px 20px rgba(17, 24, 39, 0.12)";
    });
    
    container.addEventListener("mouseleave", () => {
      container.style.background = "linear-gradient(135deg, #f3f1ec 0%, #faf9f6 100%)";
      container.style.boxShadow = "0 4px 14px rgba(17, 24, 39, 0.08)";
    });

    // Lock Icon using utility
    const lockIcon = Icons.createIconSync("lock", {
      width: "16",
      height: "16",
      style: "flex-shrink: 0; color: #1b1d20;"
    });

    // Text content
    const textDiv = document.createElement("div");
    textDiv.style.cssText = "display: flex; flex-direction: column; gap: 2px; flex: 1;";
    
    const title = document.createElement("span");
    title.textContent = "Password Manager";
    title.style.cssText = "font-weight: 600; font-size: 13px; color: #1b1d20;";
    
    const subtitle = document.createElement("span");
    subtitle.textContent = "Fill with saved password";
    subtitle.style.cssText = "font-size: 12px; color: #6d7278;";

    textDiv.appendChild(title);
    textDiv.appendChild(subtitle);

    // Chevron Arrow using utility
    const chevronIcon = Icons.createIconSync("chevron-right", {
      width: "16",
      height: "16",
      style: "flex-shrink: 0; color: #6d7278;"
    });

    if (lockIcon) container.appendChild(lockIcon);
    container.appendChild(textDiv);
    if (chevronIcon) container.appendChild(chevronIcon);

    container.addEventListener("click", () => {
      chrome.runtime.sendMessage({ type: "OPEN_POPUP", urlPath: `popup.html?mode=autofill&domain=${encodeURIComponent(domain)}` });
    });

    passwordField.insertAdjacentElement("afterend", container);
  }

  const pw = findPasswordField();
  if (!pw) return;

  function checkAndInject() {
    chrome.runtime.sendMessage({ type: "CHECK_DOMAIN", domain }, (res) => {
      if (res && res.exists) {
        // Remove existing suggestion if present
        const existing = document.querySelector(".pm-suggestion-container");
        if (!existing) {
          injectPasswordSuggestion(pw);
        }
      }
    });
  }

  // Check initially
  checkAndInject();

  // Listen for domainMap updates from popup
  chrome.runtime.onMessage.addListener((msg, sender, sendResponse) => {
    if (msg && msg.type === "DOMAIN_MAP_UPDATED") {
      checkAndInject();
    }
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
