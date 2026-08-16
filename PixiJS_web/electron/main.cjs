/**
 * Electron shell for PixiJS 江湖 OL — fixed portrait phone layout.
 * Serves vite `dist/` via `pix://` so absolute `/JHOnlineData/…` and
 * relative `./…` both resolve (plain `file://` breaks leading-slash fetches).
 *
 * CommonJS on purpose: Electron packaged main + project `"type":"module"`
 * is more reliable as .cjs (avoids ESM parse surprises in asar).
 */
const { app, BrowserWindow, net, protocol, shell } = require("electron");
const fs = require("node:fs");
const path = require("node:path");
const { pathToFileURL } = require("node:url");

const DIST_DIR = path.join(__dirname, "..", "dist");

/**
 * Content size ≈ mobile stage: LCD 240×400 scaled to width + HTML #touch-bar.
 * 360×800 keeps 9:20 phone-ish ratio (LCD band 360×600 + ~200px controls).
 */
const PORTRAIT_W = 360;
const PORTRAIT_H = 800;

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

function resolveDistFile(urlPath) {
  let rel = decodeURIComponent(urlPath || "/");
  if (!rel || rel === "/") rel = "/index.html";
  // Strip host-style prefix if present (pix://app/index.html → /index.html).
  rel = rel.replace(/^\/+/, "");
  const candidate = path.normalize(path.join(DIST_DIR, rel));
  const root = path.normalize(DIST_DIR + path.sep);
  if (!candidate.startsWith(root) && candidate !== path.normalize(DIST_DIR)) {
    return path.join(DIST_DIR, "index.html");
  }
  return candidate;
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
}

app.whenReady().then(() => {
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

  createWindow();
  app.on("activate", () => {
    if (BrowserWindow.getAllWindows().length === 0) createWindow();
  });
});

app.on("window-all-closed", () => {
  if (process.platform !== "darwin") app.quit();
});
