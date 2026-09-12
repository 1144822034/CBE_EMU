/**
 * Build no-FX APK: MAP_COSMETIC_ATLAS off, skip chibang/zuoqi/chenghao assets.
 * Does not bump versionCode / client_code_version.
 */
import { spawnSync } from "node:child_process";
import path from "node:path";
import { fileURLToPath } from "node:url";

const pixiRoot = path.resolve(path.dirname(fileURLToPath(import.meta.url)), "..");
const env = {
  ...process.env,
  VITE_MAP_COSMETIC_ATLAS: "0",
  PIXI_SKIP_COSMETIC_ASSETS: "1",
};

function run(cmd, args) {
  const r = spawnSync(cmd, args, {
    cwd: pixiRoot,
    env,
    stdio: "inherit",
    shell: true,
  });
  if ((r.status ?? 1) !== 0) process.exit(r.status ?? 1);
}

console.log("[build-android-nofx] VITE_MAP_COSMETIC_ATLAS=0, skip cosmetic assets");
run("npm", ["run", "build"]);
run("npm", ["run", "cap:sync"]);
run("node", ["scripts/assemble-android.mjs", "jh_ol_nofx.apk"]);
