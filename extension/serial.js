export class VaultKeySerial {
  constructor() {
    this.port = null;
    this.reader = null;
    this.writer = null;
    this.decoder = new TextDecoder();
    this.encoder = new TextEncoder();
    this.buf = "";
    this.waiters = [];
    this.disconnectCallbacks = [];
    this._disconnectListener = (e) => {
      if (e && e.target === this.port) this._handleDisconnect();
    };
  }

  onDisconnect(cb) {
    this.disconnectCallbacks.push(cb);
  }

  async connect({ auto = false } = {}) {
    if (!("serial" in navigator)) throw new Error("WebSerial not supported");

    if (auto) {
      const ports = await navigator.serial.getPorts();
      this.port = ports && ports.length ? ports[0] : await navigator.serial.requestPort();
    } else {
      this.port = await navigator.serial.requestPort();
    }

    await this.port.open({ baudRate: 115200 });
    this.writer = this.port.writable.getWriter();
    this.reader = this.port.readable.getReader();
    navigator.serial.addEventListener("disconnect", this._disconnectListener);
    this._readLoop();
  }

  async disconnect() {
    try {
      navigator.serial.removeEventListener("disconnect", this._disconnectListener);
    } catch {}

    try {
      if (this.reader) await this.reader.cancel();
    } catch {}
    try {
      if (this.reader) this.reader.releaseLock();
    } catch {}
    try {
      if (this.writer) this.writer.releaseLock();
    } catch {}
    try {
      if (this.port) await this.port.close();
    } catch {}

    this.port = null;
    this.reader = null;
    this.writer = null;
    this.buf = "";
    this._rejectAll(new Error("disconnected"));
  }

  async send(cmdObject) {
    if (!this.writer) throw new Error("not connected");
    const line = JSON.stringify(cmdObject) + "\n";
    await this.writer.write(this.encoder.encode(line));
  }

  async command(cmdObj, timeoutMs = 3000) {
    let lastErr = null;
    for (let attempt = 0; attempt < 2; attempt++) {
      await this.send(cmdObj);
      try {
        return await this._nextMessage(timeoutMs);
      } catch (e) {
        lastErr = e;
        if (e && e.code === "malformed_json" && attempt === 0) continue;
        throw e;
      }
    }
    throw lastErr || new Error("timeout");
  }

  _nextMessage(timeoutMs) {
    return new Promise((resolve, reject) => {
      const timer = setTimeout(() => {
        reject(Object.assign(new Error("timeout"), { code: "timeout" }));
      }, timeoutMs);

      this.waiters.push({
        resolve: (msg) => {
          clearTimeout(timer);
          resolve(msg);
        },
        reject: (err) => {
          clearTimeout(timer);
          reject(err);
        }
      });
    });
  }

  _rejectAll(err) {
    const ws = this.waiters.slice();
    this.waiters = [];
    for (const w of ws) w.reject(err);
  }

  _handleDisconnect() {
    const err = Object.assign(new Error("disconnected"), { code: "disconnected" });
    this._rejectAll(err);
    for (const cb of this.disconnectCallbacks) {
      try {
        cb();
      } catch {}
    }
  }

  async _readLoop() {
    try {
      while (true) {
        const { value, done } = await this.reader.read();
        if (done) break;
        this.buf += this.decoder.decode(value, { stream: true });

        while (true) {
          const idx = this.buf.indexOf("\n");
          if (idx < 0) break;
          const rawLine = this.buf.slice(0, idx);
          this.buf = this.buf.slice(idx + 1);
          const line = rawLine.trim();
          if (!line) continue;

          // Skip non-JSON lines (debug output, etc.)
          if (!line.startsWith('{') && !line.startsWith('[')) continue;

          let msg = null;
          try {
            msg = JSON.parse(line);
          } catch {
            const err = Object.assign(new Error("malformed_json"), { code: "malformed_json", raw: line });
            const w = this.waiters.shift();
            if (w) w.reject(err);
            continue;
          }

          const w = this.waiters.shift();
          if (w) w.resolve(msg);
        }
      }
    } catch {
      // Fall through to disconnect handler
    }
    this._handleDisconnect();
  }
}

