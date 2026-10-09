/**
 * Electron shell for PixiJS 江湖 OL — fixed portrait phone layout.
 * Serves vite `dist/` via `pix://` so absolute `/JHOnlineData/…` and
 * relative `./…` both resolve (plain `file://` breaks leading-slash fetches).
 *
 * Packaged builds also run dist hot-update (same contract as Android, but
 * only in this Electron process — Android DistHotUpdate is untouched).
 *
 * CommonJS on purpose: Electron packaged main + project `"type":"module"`
 * is more reliable as .cjs (avoids ESM parse surprises in asar).
 */
const { app, BrowserWindow, ipcMain, net, protocol, shell, session } = require("electron");
const fs = require("node:fs");
const path = require("node:path");
const { pathToFileURL } = require("node:url");
const distHot = require("./dist-hot.cjs");
const cbeLaunch = require("./cbe-launch.cjs");

const DIST_DIR = path.join(__dirname, "..", "dist");
/** Dev `npm run electron` keeps using the local dist tree as-is. */
const HOT_UPDATE_ENABLED = app.isPackaged;

/**
 * Content size ≈ mobile stage: LCD 240×400 scaled to width + HTML #touch-bar.
 * 360×800 keeps 9:20 phone-ish ratio (LCD band 360×600 + ~200px controls).
 */
const PORTRAIT_W = 360;
const PORTRAIT_H = 800;

const AUDIO_PROBE = `(function(){
  if (window.__jhAudioProbe) return;
  window.__jhAudioProbe = 1;
  function titleOf() {
    var nodes = document.querySelectorAll("h2, .song-name, .song-title");
    for (var i = 0; i < nodes.length; i++) {
      var t = (nodes[i].textContent || "").replace(/\\s+/g, " ").trim();
      if (!t || t === "歌曲名称" || t === "歌手名称" || t.length > 80) continue;
      return t;
    }
    return "";
  }
  function send() {
    var a = document.querySelector("audio");
    var src = (a && (a.currentSrc || a.src)) || "";
    if (!src || src.indexOf("http") !== 0) return;
    try {
      parent.postMessage({ type: "jh-ent-audio", url: src, title: titleOf() }, "*");
    } catch (e) {}
  }
  setInterval(send, 1000);
  document.addEventListener("play", send, true);
})();`;

function isMusicFileUrl(raw) {
  let url;
  try {
    url = new URL(raw);
  } catch {
    return false;
  }
  if (url.protocol !== "http:" && url.protocol !== "https:") return false;
  const host = url.hostname.toLowerCase();
  if (host === "localhost" || host === "127.0.0.1") return false;
  const path = url.pathname.toLowerCase();
  if (/\.(mp4|m3u8|webm|jpe?g|png|gif|webp|js|css|html|json|lrc)$/.test(path)) return false;
  if (path.includes("/api/")) return false;
  if (/\.(mp3|flac|m4a|aac|wav|ogg|ape|wma)$/.test(path)) return true;
  const q = url.search.toLowerCase();
  if (
    q.includes("format=mp3") ||
    q.includes("format=flac") ||
    q.includes("format=aac") ||
    q.includes("format=m4a") ||
    q.includes("type=convert_url")
  ) {
    return true;
  }
  return (
    host.includes("kuwo.cn") ||
    host.includes("qqmusic") ||
    host.includes("music.126.net") ||
    host.includes("kugou.com")
  );
}

let lastNotedMusic = "";

function publishMusicUrl(url, title) {
  const payload = { type: "jh-ent-audio", url };
  if (title) payload.title = title;
  const js = `window.postMessage(${JSON.stringify(payload)}, "*")`;
  for (const win of BrowserWindow.getAllWindows()) {
    if (win.isDestroyed()) continue;
    win.webContents.executeJavaScript(js).catch(() => undefined);
  }
}

function noteMusicFromElectron(details) {
  const url = details && details.url;
  if (!url || url === lastNotedMusic) return;
  if (details.statusCode && details.statusCode >= 400) return;
  if (!isMusicFileUrl(url)) return;
  lastNotedMusic = url;
  publishMusicUrl(url);
}

function probeMusicFrames(win) {
  if (!win || win.isDestroyed()) return;
  const frames = [];
  const walk = (frame) => {
    if (!frame) return;
    frames.push(frame);
    const kids = frame.frames || [];
    for (const child of kids) walk(child);
  };
  try {
    walk(win.webContents.mainFrame);
  } catch {
    return;
  }
  for (const frame of frames) {
    let url = "";
    try {
      url = frame.url || "";
    } catch {
      continue;
    }
    if (!/qqmp3\.vip/i.test(url)) continue;
    frame.executeJavaScript(AUDIO_PROBE).catch(() => undefined);
  }
}

function installMusicSniff() {
  session.defaultSession.webRequest.onCompleted(
    { urls: ["http://*/*", "https://*/*"] },
    (details) => {
      try {
        noteMusicFromElectron(details);
      } catch (e) {
        console.warn("[electron] music sniff", e);
      }
    },
  );
}

protocol.registerSchemesAsPrivileged([
  {
    scheme: "pix",
    privileges: {
      standard: true,
      secure: true,
      supportFetchAPI: true,
      corsEnabled: true,
      stream: true,
    },
  },
]);

function resolveRel(urlPath) {
  let rel = decodeURIComponent(urlPath || "/");
  if (!rel || rel === "/") rel = "/index.html";
  // Strip host-style prefix if present (pix://app/index.html → /index.html).
  rel = rel.replace(/^\/+/, "");
  return rel || "index.html";
}

function resolveDistFile(urlPath) {
  const rel = resolveRel(urlPath);
  if (HOT_UPDATE_ENABLED) {
    const hot = distHot.resolveHotFile(rel);
    if (hot) return hot;
  }
  const candidate = path.normalize(path.join(DIST_DIR, rel));
  const root = path.normalize(DIST_DIR + path.sep);
  if (!candidate.startsWith(root) && candidate !== path.normalize(DIST_DIR)) {
    return path.join(DIST_DIR, "index.html");
  }
  return candidate;
}

function createSplashWindow() {
  const win = new BrowserWindow({
    width: PORTRAIT_W,
    height: PORTRAIT_H,
    minWidth: 300,
    minHeight: Math.round((300 * PORTRAIT_H) / PORTRAIT_W),
    useContentSize: true,
    resizable: false,
    maximizable: false,
    fullscreenable: false,
    title: "江湖 OL",
    backgroundColor: "#05070a",
    autoHideMenuBar: true,
    show: true,
    webPreferences: {
      contextIsolation: true,
      nodeIntegration: false,
      sandbox: true,
    },
  });
  win.setAspectRatio(PORTRAIT_W / PORTRAIT_H);
  const html =
    "<!doctype html><html><body style=\"margin:0;background:#05070a;color:#c4a35a;" +
    "font:16px sans-serif;display:flex;align-items:center;justify-content:center;" +
    "height:100vh\">正在检查资源…</body></html>";
  void win.loadURL("data:text/html;charset=utf-8," + encodeURIComponent(html));
  return win;
}

function setSplashText(win, text) {
  if (!win || win.isDestroyed()) return;
  const js =
    "document.body && (document.body.textContent=" + JSON.stringify(String(text)) + ")";
  win.webContents.executeJavaScript(js).catch(() => undefined);
}

function createWindow() {
  const win = new BrowserWindow({
    width: PORTRAIT_W,
    height: PORTRAIT_H,
    minWidth: 300,
    minHeight: Math.round((300 * PORTRAIT_H) / PORTRAIT_W),
    useContentSize: true,
    resizable: true,
    maximizable: false,
    fullscreenable: false,
    title: "江湖 OL",
    backgroundColor: "#05070a",
    autoHideMenuBar: true,
    webPreferences: {
      preload: path.join(__dirname, "preload.cjs"),
      contextIsolation: true,
      nodeIntegration: false,
      sandbox: true,
    },
  });

  win.setAspectRatio(PORTRAIT_W / PORTRAIT_H);
  void win.loadURL("pix://app/index.html");

  win.webContents.setWindowOpenHandler(({ url }) => {
    void shell.openExternal(url);
    return { action: "deny" };
  });

  const probeTimer = setInterval(() => probeMusicFrames(win), 2000);
  win.on("closed", () => clearInterval(probeTimer));
  return win;
}

async function bootWithHotUpdate() {
  if (!HOT_UPDATE_ENABLED) {
    createWindow();
    return;
  }
  const splash = createSplashWindow();
  let banner = null;
  try {
    const downloaded = await distHot.sync({
      distDir: DIST_DIR,
      hotRoot: path.join(app.getPath("userData"), "dist-hot"),
      appVersion: app.getVersion(),
      onProgress: (text) => setSplashText(splash, text),
    });
    if (downloaded > 0) banner = "资源已更新";
  } catch (e) {
    console.warn("[electron] dist hot update failed; using local assets", e);
    banner = "网络不可用，使用本地资源";
  }
  if (banner) setSplashText(splash, banner);
  createWindow();
  if (!splash.isDestroyed()) splash.close();
}

app.whenReady().then(() => {
  installMusicSniff();
  protocol.handle("pix", (request) => {
    try {
      const u = new URL(request.url);
      const filePath = resolveDistFile(u.pathname);
      if (!fs.existsSync(filePath) || fs.statSync(filePath).isDirectory()) {
        return new Response("Not Found", { status: 404 });
      }
      return net.fetch(pathToFileURL(filePath).href);
    } catch (e) {
      console.error("[electron] pix protocol", request.url, e);
      return new Response("Bad Request", { status: 400 });
    }
  });

  ipcMain.handle("cbe:launch", async () => {
    try {
      return await cbeLaunch.launchCbe();
    } catch (e) {
      const msg = e instanceof Error ? e.message : String(e);
      console.warn("[electron] cbe launch failed", e);
      return { ok: false, error: msg };
    }
  });

  void bootWithHotUpdate();
  app.on("activate", () => {
    if (BrowserWindow.getAllWindows().length === 0) void bootWithHotUpdate();
  });
});

app.on("window-all-closed", () => {
  if (process.platform !== "darwin") app.quit();
});
