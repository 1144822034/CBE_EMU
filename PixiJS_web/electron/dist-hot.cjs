/**
 * PC dist hot update. Same contract as Android DistHotUpdate:
 * bundled dist stays the baseline; only sha256 mismatches from BASE_URL
 * are stored under hotRoot/files and preferred by the pix:// handler.
 *
 * Network failure keeps the last good overlay plus the packaged dist.
 */
const crypto = require("node:crypto");
const fs = require("node:fs");
const http = require("node:http");
const https = require("node:https");
const path = require("node:path");

const BASE_URL = "http://106.54.59.92:8081";
const MANIFEST_NAME = "hot-manifest.json";
const MANIFEST_MAX_BYTES = 12 * 1024 * 1024;
const CONNECT_TIMEOUT_MS = 4000;
const MANIFEST_READ_TIMEOUT_MS = 12000;
const FILE_READ_TIMEOUT_MS = 60000;
const DOWNLOAD_PARALLELISM = 3;

/** rel → absolute path. Only files that differ from the bundled dist. */
let serveIndex = new Map();

function isSafeRelative(rel) {
  if (!rel || rel.startsWith("/") || rel.includes("\\") || rel.includes("\0")) return false;
  if (rel.includes("..")) return false;
  return true;
}

function resolveUnder(root, rel) {
  if (!isSafeRelative(rel)) return null;
  const rootNorm = path.resolve(root);
  const dest = path.resolve(rootNorm, ...rel.split("/"));
  const relCheck = path.relative(rootNorm, dest);
  if (!relCheck || relCheck.startsWith("..") || path.isAbsolute(relCheck)) return null;
  return dest;
}

function resolveHotFile(rel) {
  if (rel == null) return null;
  let key = String(rel).replace(/^\/+/, "");
  if (!key) key = "index.html";
  return serveIndex.get(key) || null;
}

function parseManifest(text) {
  const root = JSON.parse(text);
  const files = root && root.files;
  const out = new Map();
  if (!files || typeof files !== "object") return out;
  for (const key of Object.keys(files)) {
    if (!isSafeRelative(key) || key === MANIFEST_NAME) continue;
    const hash = String(files[key] || "").toLowerCase();
    if (/^[0-9a-f]{64}$/.test(hash)) out.set(key, hash);
  }
  return out;
}

function readManifestFile(filePath) {
  try {
    if (!fs.existsSync(filePath)) return new Map();
    return parseManifest(fs.readFileSync(filePath, "utf8"));
  } catch (e) {
    console.warn("[dist-hot] manifest unreadable", filePath, e && e.message ? e.message : e);
    return new Map();
  }
}

function sha256File(filePath) {
  return new Promise((resolve, reject) => {
    const hash = crypto.createHash("sha256");
    const input = fs.createReadStream(filePath);
    input.on("error", reject);
    hash.on("error", reject);
    input.pipe(hash);
    hash.on("finish", () => resolve(hash.digest("hex")));
  });
}

function fileUrl(baseUrl, rel) {
  const parts = rel.split("/").map((part) => encodeURIComponent(part));
  return `${baseUrl.replace(/\/+$/, "")}/${parts.join("/")}`;
}

function requestBuffer(url, timeoutMs, maxBytes, redirectsLeft) {
  return new Promise((resolve, reject) => {
    let settled = false;
    const fail = (err) => {
      if (settled) return;
      settled = true;
      reject(err);
    };
    const ok = (value) => {
      if (settled) return;
      settled = true;
      resolve(value);
    };
    let parsed;
    try {
      parsed = new URL(url);
    } catch (e) {
      fail(e);
      return;
    }
    const lib = parsed.protocol === "https:" ? https : http;
    const req = lib.get(
      url,
      {
        headers: {
          "Accept-Encoding": "identity",
          "Cache-Control": "no-cache",
        },
      },
      (res) => {
        const code = res.statusCode || 0;
        if (code >= 300 && code < 400 && res.headers.location) {
          res.resume();
          if (redirectsLeft <= 0) {
            fail(new Error("too many redirects " + url));
            return;
          }
          const next = new URL(res.headers.location, url);
          if (next.protocol !== "http:" && next.protocol !== "https:") {
            fail(new Error("bad redirect " + next.protocol));
            return;
          }
          requestBuffer(next.href, timeoutMs, maxBytes, redirectsLeft - 1).then(ok, fail);
          return;
        }
        if (code !== 200) {
          res.resume();
          fail(new Error("HTTP " + code + " " + url));
          return;
        }
        const chunks = [];
        let total = 0;
        res.on("data", (chunk) => {
          total += chunk.length;
          if (maxBytes != null && total > maxBytes) {
            req.destroy(new Error("body exceeds " + maxBytes));
            return;
          }
          chunks.push(chunk);
        });
        res.on("end", () => ok(Buffer.concat(chunks)));
        res.on("error", fail);
      },
    );
    req.on("error", fail);
    req.setTimeout(timeoutMs, () => req.destroy(new Error("timeout " + url)));
  });
}

function downloadToFile(url, dest, timeoutMs, redirectsLeft) {
  return new Promise((resolve, reject) => {
    let settled = false;
    const fail = (err) => {
      if (settled) return;
      settled = true;
      reject(err);
    };
    const ok = () => {
      if (settled) return;
      settled = true;
      resolve();
    };
    const req = (url.startsWith("https:") ? https : http).get(
      url,
      { headers: { "Accept-Encoding": "identity", "Cache-Control": "no-cache" } },
      (res) => {
        const code = res.statusCode || 0;
        if (code >= 300 && code < 400 && res.headers.location) {
          res.resume();
          if (redirectsLeft <= 0) {
            fail(new Error("too many redirects " + url));
            return;
          }
          const next = new URL(res.headers.location, url);
          if (next.protocol !== "http:" && next.protocol !== "https:") {
            fail(new Error("bad redirect " + next.protocol));
            return;
          }
          downloadToFile(next.href, dest, timeoutMs, redirectsLeft - 1).then(ok, fail);
          return;
        }
        if (code !== 200) {
          res.resume();
          fail(new Error("HTTP " + code + " " + url));
          return;
        }
        const file = fs.createWriteStream(dest);
        res.pipe(file);
        file.on("finish", () => file.close((err) => (err ? fail(err) : ok())));
        file.on("error", fail);
        res.on("error", fail);
      },
    );
    req.on("error", fail);
    req.setTimeout(timeoutMs, () => req.destroy(new Error("timeout " + url)));
  });
}

async function downloadVerified(baseUrl, rel, expectHash, dest) {
  fs.mkdirSync(path.dirname(dest), { recursive: true });
  const tmp = dest + ".part";
  try {
    await downloadToFile(fileUrl(baseUrl, rel), tmp, FILE_READ_TIMEOUT_MS, 5);
    const got = await sha256File(tmp);
    if (got !== expectHash.toLowerCase()) throw new Error("hash mismatch " + rel);
    if (fs.existsSync(dest)) fs.rmSync(dest);
    fs.renameSync(tmp, dest);
  } catch (err) {
    fs.rmSync(tmp, { force: true });
    throw err;
  }
}

async function mapPool(items, limit, fn) {
  if (items.length === 0) return;
  let next = 0;
  let failure = null;
  const workers = [];
  const count = Math.min(limit, items.length);
  for (let i = 0; i < count; i++) {
    workers.push(
      (async () => {
        while (!failure) {
          const index = next++;
          if (index >= items.length) return;
          try {
            await fn(items[index], index);
          } catch (e) {
            if (!failure) failure = e;
            throw e;
          }
        }
      })(),
    );
  }
  await Promise.allSettled(workers);
  if (failure) throw failure;
}

function writeApplied(filePath, applied) {
  const files = {};
  for (const [rel, hash] of applied) files[rel] = hash;
  const tmp = filePath + ".tmp";
  fs.mkdirSync(path.dirname(filePath), { recursive: true });
  fs.writeFileSync(tmp, JSON.stringify({ files }), "utf8");
  if (fs.existsSync(filePath)) fs.rmSync(filePath);
  fs.renameSync(tmp, filePath);
}

async function healApplied(filesRoot, applied) {
  for (const [rel, hash] of [...applied]) {
    const file = resolveUnder(filesRoot, rel);
    let ok = false;
    try {
      ok = !!file && fs.statSync(file).isFile() && (await sha256File(file)) === hash;
    } catch {
      ok = false;
    }
    if (!ok) {
      applied.delete(rel);
      if (file && fs.existsSync(file)) {
        try {
          fs.rmSync(file, { force: true });
        } catch (e) {
          console.warn("[dist-hot] heal delete failed", file, e && e.message ? e.message : e);
        }
      }
    }
  }
}

function setServe(filesRoot, applied) {
  const next = new Map();
  for (const rel of applied.keys()) {
    const abs = resolveUnder(filesRoot, rel);
    if (abs && fs.existsSync(abs)) next.set(rel, abs);
  }
  serveIndex = next;
}

function prune(dir, prefix, keep) {
  let kids = [];
  try {
    kids = fs.readdirSync(dir);
  } catch {
    return;
  }
  for (const name of kids) {
    const rel = prefix ? `${prefix}/${name}` : name;
    const abs = path.join(dir, name);
    let st;
    try {
      st = fs.statSync(abs);
    } catch {
      continue;
    }
    if (st.isDirectory()) {
      prune(abs, rel, keep);
      try {
        if (fs.readdirSync(abs).length === 0) fs.rmdirSync(abs);
      } catch {
        /* ignore */
      }
    } else if (!keep.has(rel)) {
      fs.rmSync(abs, { force: true });
    }
  }
}

function sameOverlay(applied, bundled, remote) {
  for (const [rel, hash] of applied) {
    const want = remote.get(rel);
    const base = bundled.get(rel);
    if (want == null || want === base) return false;
    if (want !== hash) return false;
  }
  return true;
}

function wipeHotRoot(hotRoot) {
  fs.rmSync(hotRoot, { recursive: true, force: true });
  serveIndex = new Map();
}

function maybeWipeForVersion(hotRoot, appVersion) {
  if (!appVersion) return;
  const stamp = path.join(hotRoot, "app-version.txt");
  let prev = "";
  try {
    prev = fs.readFileSync(stamp, "utf8").trim();
  } catch {
    prev = "";
  }
  if (prev === appVersion) return;
  console.log("[dist-hot] app version", prev || "(none)", "->", appVersion, "; wiping overlay");
  wipeHotRoot(hotRoot);
  fs.mkdirSync(hotRoot, { recursive: true });
  fs.writeFileSync(stamp, appVersion, "utf8");
}

/**
 * @param {{ distDir: string, hotRoot: string, appVersion?: string, baseUrl?: string, onProgress?: (text: string) => void }} opts
 * @returns {Promise<number>} files downloaded
 */
async function sync(opts) {
  const distDir = opts.distDir;
  const hotRoot = opts.hotRoot;
  const baseUrl = (opts.baseUrl || BASE_URL).replace(/\/+$/, "");
  const onProgress = typeof opts.onProgress === "function" ? opts.onProgress : () => undefined;

  maybeWipeForVersion(hotRoot, opts.appVersion);

  const filesRoot = path.join(hotRoot, "files");
  const staging = path.join(hotRoot, "staging");
  fs.mkdirSync(filesRoot, { recursive: true });
  fs.rmSync(staging, { recursive: true, force: true });
  fs.mkdirSync(staging, { recursive: true });

  const bundled = readManifestFile(path.join(distDir, MANIFEST_NAME));
  if (bundled.size === 0) {
    console.warn("[dist-hot] bundled hot-manifest missing; skip download");
    serveIndex = new Map();
    return 0;
  }

  const applied = readManifestFile(path.join(hotRoot, "applied.json"));
  await healApplied(filesRoot, applied);
  writeApplied(path.join(hotRoot, "applied.json"), applied);
  setServe(filesRoot, applied);

  let remote;
  try {
    const buf = await requestBuffer(
      `${baseUrl}/${MANIFEST_NAME}`,
      Math.max(CONNECT_TIMEOUT_MS, MANIFEST_READ_TIMEOUT_MS),
      MANIFEST_MAX_BYTES,
      5,
    );
    remote = parseManifest(buf.toString("utf8"));
  } catch (e) {
    fs.rmSync(staging, { recursive: true, force: true });
    throw e;
  }
  if (remote.size === 0) {
    console.warn("[dist-hot] remote hot-manifest empty; keep local");
    fs.rmSync(staging, { recursive: true, force: true });
    return 0;
  }

  const toDownload = [];
  for (const [rel, want] of remote) {
    // Remote matches the packaged file → keep using dist/, drop any overlay later.
    if (want === bundled.get(rel)) continue;
    let have = null;
    const overlay = resolveUnder(filesRoot, rel);
    if (applied.has(rel) && overlay && fs.existsSync(overlay)) have = applied.get(rel);
    if (have == null) have = bundled.get(rel) || null;
    if (want !== have) toDownload.push(rel);
  }
  if (toDownload.length === 0 && sameOverlay(applied, bundled, remote)) {
    console.log("[dist-hot] dist matches hot manifest (" + remote.size + " files)");
    fs.rmSync(staging, { recursive: true, force: true });
    return 0;
  }

  let finished = 0;
  if (toDownload.length > 0) {
    console.log("[dist-hot] downloading " + toDownload.length + " file(s) from " + baseUrl);
    try {
      await mapPool(toDownload, DOWNLOAD_PARALLELISM, async (rel) => {
        const dest = resolveUnder(staging, rel);
        if (!dest) throw new Error("bad path " + rel);
        await downloadVerified(baseUrl, rel, remote.get(rel), dest);
        finished += 1;
        onProgress("正在下载资源 " + finished + "/" + toDownload.length);
      });
    } catch (e) {
      fs.rmSync(staging, { recursive: true, force: true });
      throw e;
    }
  } else {
    console.log("[dist-hot] pruning overlay to match remote (" + remote.size + " files)");
  }

  const next = new Map();
  for (const [rel, want] of remote) {
    if (want === bundled.get(rel)) continue;
    const staged = resolveUnder(staging, rel);
    const dest = resolveUnder(filesRoot, rel);
    if (!dest) throw new Error("bad path " + rel);
    if (staged && fs.existsSync(staged)) {
      fs.mkdirSync(path.dirname(dest), { recursive: true });
      if (fs.existsSync(dest)) fs.rmSync(dest);
      try {
        fs.renameSync(staged, dest);
      } catch {
        fs.copyFileSync(staged, dest);
        fs.rmSync(staged, { force: true });
      }
    } else if (!(fs.existsSync(dest) && want === applied.get(rel))) {
      throw new Error("missing staged file " + rel);
    }
    next.set(rel, want);
  }
  prune(filesRoot, "", next);
  writeApplied(path.join(hotRoot, "applied.json"), next);
  fs.rmSync(staging, { recursive: true, force: true });
  setServe(filesRoot, next);
  console.log("[dist-hot] overlay files=" + next.size + " downloaded=" + toDownload.length);
  return toDownload.length;
}

async function selfTest() {
  const os = require("node:os");
  const root = fs.mkdtempSync(path.join(os.tmpdir(), "dist-hot-"));
  const distDir = path.join(root, "dist");
  const hotRoot = path.join(root, "hot");
  fs.mkdirSync(path.join(distDir, "assets"), { recursive: true });
  fs.writeFileSync(path.join(distDir, "index.html"), "v1", "utf8");
  fs.writeFileSync(path.join(distDir, "assets", "a.js"), "same", "utf8");
  const hash = (text) => crypto.createHash("sha256").update(text).digest("hex");
  const bundled = {
    files: {
      "index.html": hash("v1"),
      "assets/a.js": hash("same"),
    },
  };
  fs.writeFileSync(path.join(distDir, MANIFEST_NAME), JSON.stringify(bundled), "utf8");

  const remoteFiles = {
    "index.html": hash("v2"),
    "assets/a.js": hash("same"),
    "assets/b.js": hash("new"),
  };
  const bodies = {
    "/index.html": "v2",
    "/assets/a.js": "same",
    "/assets/b.js": "new",
    "/hot-manifest.json": JSON.stringify({ files: remoteFiles }),
  };
  const server = http.createServer((req, res) => {
    const body = bodies[req.url];
    if (body == null) {
      res.writeHead(404);
      res.end();
      return;
    }
    res.writeHead(200);
    res.end(body);
  });
  await new Promise((resolve) => server.listen(0, "127.0.0.1", resolve));
  const { port } = server.address();
  const baseUrl = `http://127.0.0.1:${port}`;
  try {
    const n = await sync({ distDir, hotRoot, appVersion: "1", baseUrl });
    if (n !== 2) throw new Error("expected 2 downloads, got " + n);
    const indexPath = resolveHotFile("index.html");
    if (!indexPath || fs.readFileSync(indexPath, "utf8") !== "v2") throw new Error("index overlay");
    if (resolveHotFile("assets/a.js")) throw new Error("unchanged file must stay bundled");
    if (fs.readFileSync(resolveHotFile("assets/b.js"), "utf8") !== "new") throw new Error("new file");

    remoteFiles["index.html"] = hash("v1");
    delete remoteFiles["assets/b.js"];
    bodies["/hot-manifest.json"] = JSON.stringify({ files: remoteFiles });
    const n2 = await sync({ distDir, hotRoot, appVersion: "1", baseUrl });
    if (n2 !== 0) throw new Error("rollback should not download, got " + n2);
    if (resolveHotFile("index.html") || resolveHotFile("assets/b.js")) {
      throw new Error("overlay should drop files that match the bundle or left the manifest");
    }

    const n3 = await sync({ distDir, hotRoot, appVersion: "2", baseUrl });
    if (n3 !== 0) throw new Error("version wipe + matching remote should not download");
    console.log("dist-hot self-test ok");
  } finally {
    server.close();
    fs.rmSync(root, { recursive: true, force: true });
  }
}

module.exports = {
  BASE_URL,
  sync,
  resolveHotFile,
  wipeHotRoot,
};

if (require.main === module && process.argv.includes("--self-test")) {
  selfTest().catch((err) => {
    console.error(err);
    process.exit(1);
  });
}
