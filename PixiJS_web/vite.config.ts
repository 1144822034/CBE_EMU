import fs from "node:fs";
import path from "node:path";
import { fileURLToPath } from "node:url";
import { defineConfig, type Plugin } from "vite";
import type { IncomingMessage, ServerResponse } from "node:http";

const rootDir = path.dirname(fileURLToPath(import.meta.url));
const repoRoot = path.resolve(rootDir, "..");

function resolveAssetRoot(): string {
  const candidates = [
    path.join(repoRoot, "web", "fs", "JHOnlineData"),
    path.join(repoRoot, "bin", "JHOnlineData"),
  ];
  for (const candidate of candidates) {
    if (fs.existsSync(candidate)) return candidate;
  }
  return candidates[0]!;
}

/** Prefer full Unicode pack for Pixi trial; keep font_gb as fallback. */
function resolveFontPath(requestName: string): string | null {
  const byName: Record<string, string[]> = {
    "/gb16.uc2.uc3": [
      path.join(repoRoot, "web", "fs", "gb16.uc2.uc3"),
      path.join(repoRoot, "bin", "gb16.uc2.uc3"),
      path.join(repoRoot, "bin", "multiplayer", "gb16.uc2.uc3"),
    ],
    "/font_16_sky.uc3": [
      path.join(repoRoot, "web", "fs", "font_16_sky.uc3"),
      path.join(repoRoot, "bin", "font_16_sky.uc3"),
    ],
    "/font_gb.uc3": [
      path.join(repoRoot, "bin", "font_gb.uc3"),
      path.join(repoRoot, "bin2", "font_gb.uc3"),
      path.join(repoRoot, "web", "fs", "font_gb.uc3"),
      path.join(repoRoot, "JianghuOL", "app", "src", "main", "assets", "font_gb.uc3"),
    ],
  };
  const candidates = byName[requestName] ?? [];
  for (const candidate of candidates) {
    if (fs.existsSync(candidate)) return candidate;
  }
  return null;
}

/** Disk stamp for client cache revalidation (mtimeMs + size). */
function resourceStamp(filePath: string): string | null {
  try {
    const st = fs.statSync(filePath);
    return `${Math.trunc(st.mtimeMs)}-${st.size}`;
  } catch {
    return null;
  }
}

function jhOnlineDataPlugin(): Plugin {
  const assetRoot = resolveAssetRoot();

  /** Resolve /JHOnlineData/<name> against disk, including GBK-percent collisions. */
  function resolveDataFile(requestName: string): string | null {
    const direct = path.join(assetRoot, requestName);
    if (fs.existsSync(direct)) return direct;

    // %CC%A9… (GBK bytes percent-encoded) decodes to Latin-1/UTF-8 mojibake.
    // Reinterpret code units ≤0xFF as GBK → real 泰山 filename.
    if (
      requestName.length > 0 &&
      [...requestName].every((ch) => (ch.codePointAt(0) ?? 0) <= 0xff)
    ) {
      try {
        const bytes = Uint8Array.from(requestName, (ch) => ch.charCodeAt(0) & 0xff);
        const gbkName = new TextDecoder("gbk").decode(bytes);
        if (gbkName !== requestName) {
          const viaGbk = path.join(assetRoot, gbkName);
          if (fs.existsSync(viaGbk)) return viaGbk;
        }
      } catch {
        /* ignore */
      }
    }

    // Last resort: ASCII fingerprint `07_02.sce` ↔ `07泰山_02.sce`.
    const m = requestName.match(/^c?(\d+).+_(\d+)(\.[^.]+)$/i);
    if (m) {
      const re = new RegExp(
        `^c?0*${m[1]}.+_${m[2]}${m[3]!.replace(".", "\\.")}$`,
        "i",
      );
      try {
        const hit = fs.readdirSync(assetRoot).find((f) => re.test(f));
        if (hit) {
          const viaFinger = path.join(assetRoot, hit);
          if (fs.existsSync(viaFinger)) return viaFinger;
        }
      } catch {
        /* ignore */
      }
    }
    return null;
  }

  function serveJhOnlineData(
    req: IncomingMessage,
    res: ServerResponse,
    next: () => void,
  ): void {
    const fontReq = req.url?.split("?")[0] ?? "";
    if (
      fontReq === "/gb16.uc2.uc3" ||
      fontReq === "/font_16_sky.uc3" ||
      fontReq === "/font_gb.uc3"
    ) {
      const fontPath = resolveFontPath(fontReq);
      if (!fontPath) {
        res.statusCode = 404;
        res.end(`missing ${fontReq}`);
        return;
      }
      res.setHeader("Content-Type", "application/octet-stream");
      res.setHeader("Cache-Control", "no-cache");
      if (req.method === "HEAD") {
        res.statusCode = 200;
        res.end();
        return;
      }
      fs.createReadStream(fontPath).pipe(res);
      return;
    }
    if (!req.url?.startsWith("/JHOnlineData/")) {
      next();
      return;
    }
    const raw = req.url.slice("/JHOnlineData/".length).split("?")[0] ?? "";
    let name = "";
    try {
      name = decodeURIComponent(raw);
    } catch {
      res.statusCode = 400;
      res.end("bad encoding");
      return;
    }
    if (!name || name.includes("..") || name.includes("/") || name.includes("\\")) {
      res.statusCode = 400;
      res.end("bad name");
      return;
    }
    const filePath = resolveDataFile(name);
    if (!filePath) {
      res.statusCode = 404;
      res.end(`missing ${name}`);
      return;
    }
    const stamp = resourceStamp(filePath);
    const etag = stamp ? `W/"${stamp}"` : undefined;
    const ext = path.extname(name).toLowerCase();
    const type =
      ext === ".json" ? "application/json" : "application/octet-stream";
    res.setHeader("Content-Type", type);
    res.setHeader("Cache-Control", "no-cache");
    if (stamp) {
      res.setHeader("X-JH-Resource-Stamp", stamp);
      res.setHeader("ETag", etag!);
    }

    const inm = req.headers["if-none-match"];
    if (etag && inm && inm === etag) {
      res.statusCode = 304;
      res.end();
      return;
    }

    if (req.method === "HEAD") {
      res.statusCode = 200;
      res.end();
      return;
    }

    fs.createReadStream(filePath).pipe(res);
  }

  return {
    name: "jh-online-data",
    configureServer(server) {
      server.middlewares.use(serveJhOnlineData);
    },
    configurePreviewServer(server) {
      server.middlewares.use(serveJhOnlineData);
    },
  };
}

export default defineConfig({
  plugins: [jhOnlineDataPlugin()],
  // Relative base so Capacitor/file hosts resolve assets correctly.
  base: "./",
  build: {
    // Android 8 + updated System WebView (~Chrome 69+) can parse this;
    // older WebViews hit Capacitor errorPath instead of a blank crash.
    target: ["es2019", "chrome69"],
    cssTarget: "chrome69",
  },
  server: {
    host: true, // 0.0.0.0 — phone / LAN can open http://<pc-ip>:5173
    port: 5173,
    fs: {
      allow: [repoRoot],
    },
  },
  preview: {
    host: true,
    port: 5173,
  },
});
