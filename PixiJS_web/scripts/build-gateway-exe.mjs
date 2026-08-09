/**
 * Bundle ws-gateway.mjs + ws → CJS, then inject into a copy of node.exe (SEA).
 *
 * Output: dist/gateway/ws-gateway.exe
 *
 * Usage:
 *   node scripts/build-gateway-exe.mjs
 *   npm run gateway:exe
 */
import { build } from "esbuild";
import { execFileSync, spawnSync } from "node:child_process";
import {
  copyFileSync,
  existsSync,
  mkdirSync,
  rmSync,
  writeFileSync,
} from "node:fs";
import { dirname, join } from "node:path";
import { fileURLToPath } from "node:url";
import { createRequire } from "node:module";

const __dirname = dirname(fileURLToPath(import.meta.url));
const root = join(__dirname, "..");
const outDir = join(root, "dist", "gateway");
const bundlePath = join(outDir, "ws-gateway.cjs");
const seaConfigPath = join(outDir, "sea-config.json");
const blobPath = join(outDir, "sea-prep.blob");
const exePath = join(outDir, "ws-gateway.exe");

mkdirSync(outDir, { recursive: true });
for (const p of [exePath, blobPath, seaConfigPath]) {
  if (existsSync(p)) rmSync(p);
}

console.log("[gateway:exe] bundling scripts/ws-gateway.mjs …");
await build({
  entryPoints: [join(root, "scripts", "ws-gateway.mjs")],
  bundle: true,
  platform: "node",
  target: "node18",
  format: "cjs",
  outfile: bundlePath,
  packages: "bundle",
  logLevel: "info",
});

writeFileSync(
  seaConfigPath,
  JSON.stringify(
    {
      main: bundlePath,
      output: blobPath,
      disableExperimentalSEAWarning: true,
    },
    null,
    2,
  ),
);

console.log("[gateway:exe] generating SEA blob …");
execFileSync(process.execPath, ["--experimental-sea-config", seaConfigPath], {
  stdio: "inherit",
  cwd: root,
});

console.log("[gateway:exe] copying node binary → ws-gateway.exe …");
copyFileSync(process.execPath, exePath);

// Official Windows Node builds are signed; strip so postject can rewrite the PE.
const signtool = spawnSync(
  "signtool",
  ["remove", "/s", exePath],
  { encoding: "utf8" },
);
if (signtool.status === 0) {
  console.log("[gateway:exe] removed Authenticode signature");
} else {
  console.log(
    "[gateway:exe] signtool unavailable or unsigned — continuing",
  );
}

const require = createRequire(import.meta.url);
let postjectCli;
try {
  postjectCli = require.resolve("postject/dist/cli.js");
} catch {
  postjectCli = null;
}

console.log("[gateway:exe] injecting SEA blob with postject …");
const postjectArgs = [
  exePath,
  "NODE_SEA_BLOB",
  blobPath,
  "--sentinel-fuse",
  "NODE_SEA_FUSE_fce680ab2cc467b6e072b8b5df1996b2",
];
if (postjectCli) {
  execFileSync(process.execPath, [postjectCli, ...postjectArgs], {
    stdio: "inherit",
    cwd: root,
  });
} else {
  execFileSync("npx", ["--yes", "postject", ...postjectArgs], {
    stdio: "inherit",
    cwd: root,
    shell: true,
  });
}

console.log(`[gateway:exe] done → ${exePath}`);
console.log(
  "[gateway:exe] run: dist\\gateway\\ws-gateway.exe  [wsPort] [tcpHost] [tcpPort] [listenHost]",
);
