/**
 * Minimal WebSocket ↔ TCP CBMS bridge (+ HTTP proxy for monster levels).
 * Usage:
 *   node scripts/ws-gateway.mjs [listenWsPort=19092] [tcpHost=127.0.0.1] [tcpPort=19090] [listenHost=0.0.0.0]
 * Env:
 *   PIX_GATEWAY_HOST / PIX_GATEWAY_PORT — listen bind (default 0.0.0.0:19092)
 *   PIX_TCP_HOST / PIX_TCP_PORT — mock-server (default 127.0.0.1:19090)
 *   PIX_ADMIN_HOST / PIX_ADMIN_PORT — admin HTTP for /api/* proxy
 *     (monster-levels, recharge/create; default same host as TCP, port 19091)
 *
 * Each CBMS request opens a short-lived TCP to the mock service (CBE host model).
 * Do NOT close the WebSocket on a single TCP failure — the browser session must
 * stay up so the client can retry login/select/role without "live: not connected".
 */
import http from "node:http";
import net from "node:net";
import os from "node:os";
import { WebSocketServer } from "ws";

const listenPort = Number(
  process.env.PIX_GATEWAY_PORT || process.argv[2] || 19092,
);
const tcpHost = process.env.PIX_TCP_HOST || process.argv[3] || "127.0.0.1";
const tcpPort = Number(process.env.PIX_TCP_PORT || process.argv[4] || 19090);
const listenHost =
  process.env.PIX_GATEWAY_HOST || process.argv[5] || "0.0.0.0";
const adminHost = process.env.PIX_ADMIN_HOST || tcpHost || "127.0.0.1";
const adminPort = Number(process.env.PIX_ADMIN_PORT || 19091);

async function exchangeOnce(reqBuf) {
  const socket = net.connect({ host: tcpHost, port: tcpPort });
  await new Promise((resolve, reject) => {
    socket.once("connect", resolve);
    socket.once("error", reject);
  });

  // Continuous buffer — do not attach one-shot `data` listeners inside pull();
  // localhost CBMR can arrive before the listener is registered and be lost.
  let pending = Buffer.alloc(0);
  let wait = null;
  let ended = null;

  const wake = () => {
    if (!wait) return;
    if (ended) {
      const { reject } = wait;
      wait = null;
      reject(ended);
      return;
    }
    if (pending.length >= wait.n) {
      const { n, resolve } = wait;
      wait = null;
      const out = pending.subarray(0, n);
      pending = pending.subarray(n);
      resolve(out);
    }
  };

  socket.on("data", (buf) => {
    pending = Buffer.concat([pending, buf]);
    wake();
  });
  socket.on("error", (e) => {
    ended = e instanceof Error ? e : new Error(String(e));
    wake();
  });
  socket.on("end", () => {
    ended = ended || new Error("tcp ended early");
    wake();
  });

  const pull = (n) =>
    new Promise((resolve, reject) => {
      if (pending.length >= n) {
        const out = pending.subarray(0, n);
        pending = pending.subarray(n);
        resolve(out);
        return;
      }
      if (ended) {
        reject(ended);
        return;
      }
      wait = { n, resolve, reject };
    });

  const cancelPull = (err) => {
    if (!wait) return;
    const { reject } = wait;
    wait = null;
    reject(err instanceof Error ? err : new Error(String(err)));
  };

  socket.write(reqBuf);

  const header = await pull(20);
  if (header.toString("ascii", 0, 4) !== "CBMR") {
    socket.destroy();
    throw new Error("not CBMR");
  }
  const bodyLen = header.readUInt32LE(12);
  const body = bodyLen ? await pull(bodyLen) : Buffer.alloc(0);
  const flags = header.readUInt32LE(8);
  const frames = [Buffer.concat([header, body])];

  // HAS_FOLLOWUP = 0x2 — read one more CBMR if present (best-effort short timeout).
  // Cancel the same pull waiter on timeout (no orphaned Promise.race loser).
  if (flags & 0x2) {
    const onTimeout = () => cancelPull(new Error("followup timeout"));
    try {
      socket.setTimeout(800);
      socket.once("timeout", onTimeout);
      const h2 = await pull(20);
      const l2 = h2.readUInt32LE(12);
      const b2 = l2 ? await pull(l2) : Buffer.alloc(0);
      frames.push(Buffer.concat([h2, b2]));
    } catch {
      /* ignore */
    } finally {
      socket.setTimeout(0);
      socket.off("timeout", onTimeout);
    }
  }
  socket.destroy();
  return frames;
}

function lanIpv4s() {
  const out = [];
  for (const list of Object.values(os.networkInterfaces())) {
    for (const info of list || []) {
      if (info.family === "IPv4" && !info.internal) out.push(info.address);
    }
  }
  return out;
}

function sendJson(res, status, body, extraHeaders = {}) {
  const payload = typeof body === "string" ? body : JSON.stringify(body);
  res.writeHead(status, {
    "Content-Type": "application/json; charset=utf-8",
    "Content-Length": Buffer.byteLength(payload),
    "Access-Control-Allow-Origin": "*",
    "Cache-Control": "no-store",
    ...extraHeaders,
  });
  res.end(payload);
}

function readRequestBody(req, limit = 64 * 1024) {
  return new Promise((resolve, reject) => {
    const chunks = [];
    let total = 0;
    req.on("data", (c) => {
      total += c.length;
      if (total > limit) {
        reject(new Error("body-too-large"));
        req.destroy();
        return;
      }
      chunks.push(c);
    });
    req.on("end", () => resolve(Buffer.concat(chunks)));
    req.on("error", reject);
  });
}

/** Proxy admin HTTP API paths used by Pixi (CORS + body forward). */
function proxyAdminApi(req, res, adminPath, { methods }) {
  const allow = methods.join(", ");
  if (req.method === "OPTIONS") {
    res.writeHead(204, {
      "Access-Control-Allow-Origin": "*",
      "Access-Control-Allow-Methods": `${allow}, OPTIONS`,
      "Access-Control-Allow-Headers": "Content-Type",
      "Access-Control-Max-Age": "86400",
    });
    res.end();
    return;
  }
  if (!methods.includes(req.method || "")) {
    sendJson(
      res,
      405,
      { ok: false, error: "method" },
      { Allow: `${allow}, OPTIONS` },
    );
    return;
  }

  const forward = async () => {
    let body = Buffer.alloc(0);
    if (req.method === "POST" || req.method === "PUT" || req.method === "PATCH") {
      try {
        body = await readRequestBody(req);
      } catch (err) {
        sendJson(res, 413, { ok: false, error: "body-too-large" });
        return;
      }
    }
    // timeout → destroy() emits "error"; both handlers must not writeHead twice
    // or Node throws and the whole gateway process exits.
    let responded = false;
    const replyJson = (status, payload) => {
      if (responded || res.headersSent || res.writableEnded) return;
      responded = true;
      sendJson(res, status, payload);
    };
    const replyRaw = (statusCode, contentType, out) => {
      if (responded || res.headersSent || res.writableEnded) return;
      responded = true;
      res.writeHead(statusCode || 502, {
        "Content-Type": contentType || "application/json; charset=utf-8",
        "Content-Length": out.length,
        "Access-Control-Allow-Origin": "*",
        "Cache-Control": "no-store",
      });
      res.end(out);
    };

    const headers = {
      "Content-Type":
        req.headers["content-type"] || "application/x-www-form-urlencoded",
      "Content-Length": body.length,
    };
    const upstream = http.request(
      {
        host: adminHost,
        port: adminPort,
        path: adminPath,
        method: req.method,
        headers,
        timeout: 15000,
      },
      (up) => {
        const chunks = [];
        up.on("data", (c) => chunks.push(c));
        up.on("end", () => {
          replyRaw(
            up.statusCode || 502,
            up.headers["content-type"] || "application/json; charset=utf-8",
            Buffer.concat(chunks),
          );
        });
      },
    );
    upstream.on("timeout", () => {
      upstream.destroy();
      replyJson(504, { ok: false, error: "admin-timeout" });
    });
    upstream.on("error", (err) => {
      console.error(`[ws-gateway] ${adminPath} proxy`, err.message || err);
      replyJson(502, { ok: false, error: "admin-unreachable" });
    });
    if (body.length) upstream.write(body);
    upstream.end();
  };

  void forward().catch((err) => {
    console.error(`[ws-gateway] ${adminPath} forward`, err.message || err);
  });
}

const server = http.createServer((req, res) => {
  const path = (req.url || "/").split("?")[0];
  if (path === "/api/monster-levels") {
    proxyAdminApi(req, res, "/api/monster-levels", { methods: ["GET"] });
    return;
  }
  if (path === "/api/recharge/create") {
    proxyAdminApi(req, res, "/api/recharge/create", { methods: ["POST"] });
    return;
  }
  if (path === "/healthz") {
    sendJson(res, 200, { ok: true, service: "pix-ws-gateway" });
    return;
  }
  sendJson(res, 404, { ok: false, error: "not-found" });
});

const wss = new WebSocketServer({ server });
server.on("error", (err) => {
  console.error("[ws-gateway] http server", err.message || err);
});
wss.on("error", (err) => {
  console.error("[ws-gateway] wss", err.message || err);
});
server.listen(listenPort, listenHost, () => {
  const tips = lanIpv4s().map((ip) => `ws://${ip}:${listenPort}`);
  console.log(
    `[ws-gateway] listen ${listenHost}:${listenPort} -> tcp://${tcpHost}:${tcpPort}`,
  );
  console.log(
    `[ws-gateway] http /api/monster-levels -> http://${adminHost}:${adminPort}/api/monster-levels`,
  );
  console.log(
    `[ws-gateway] http /api/recharge/create -> http://${adminHost}:${adminPort}/api/recharge/create`,
  );
  if (tips.length) {
    console.log(`[ws-gateway] phone / LAN: ${tips.join("  ")}`);
  } else {
    console.log(`[ws-gateway] local: ws://127.0.0.1:${listenPort}`);
  }
});

// Keep the bridge up: one bad proxy/WS edge must not tear down the process.
process.on("uncaughtException", (err) => {
  console.error("[ws-gateway] uncaughtException", err?.stack || err);
});
process.on("unhandledRejection", (err) => {
  console.error("[ws-gateway] unhandledRejection", err?.stack || err);
});

wss.on("connection", (ws) => {
  // Serialize per-socket exchanges so overlapping browser sends cannot interleave.
  let chain = Promise.resolve();

  ws.on("error", (err) => {
    console.error("[ws-gateway] ws", err.message || err);
  });

  ws.on("message", (data) => {
    chain = chain
      .then(async () => {
        if (ws.readyState !== ws.OPEN) return;
        try {
          const req = Buffer.isBuffer(data) ? data : Buffer.from(data);
          const frames = await exchangeOnce(req);
          for (const frame of frames) {
            if (ws.readyState === ws.OPEN) ws.send(frame);
          }
        } catch (err) {
          console.error("[ws-gateway]", err.message || err);
          // Keep WS alive — client will time out / retry on the same session.
        }
      })
      .catch((err) => {
        console.error("[ws-gateway] chain", err.message || err);
      });
  });
});
