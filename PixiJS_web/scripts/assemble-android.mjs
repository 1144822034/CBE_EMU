/**
 * Assemble debug APK using local JDK 21 + cached Gradle 8.11.1
 * (avoids wrapper re-download timeouts).
 */
import { spawnSync } from "node:child_process";
import fs from "node:fs";
import os from "node:os";
import path from "node:path";
import { fileURLToPath } from "node:url";

const pixiRoot = path.resolve(path.dirname(fileURLToPath(import.meta.url)), "..");
const androidDir = path.join(pixiRoot, "android");
const home = os.homedir();

/** Prefer Cap7-ready JDK 21; ignore env JAVA_HOME if it is 8/11/17. */
const jdkCandidates = [
  path.join(home, ".gradle", "jdks", "eclipse_adoptium-21-amd64-windows.2"),
  process.env.JAVA_HOME_21,
  process.env.JAVA_HOME,
  "D:\\installsoft\\JDK\\jdk11",
].filter(Boolean);

const gradleBat = path.join(
  home,
  ".gradle",
  "wrapper",
  "dists",
  "gradle-8.11.1-all",
  "6gcpoccneql1b0krsle0llw37",
  "gradle-8.11.1",
  "bin",
  "gradle.bat",
);

function javaMajor(javaHome) {
  const javaExe = path.join(javaHome, "bin", "java.exe");
  if (!fs.existsSync(javaExe)) return 0;
  const r = spawnSync(javaExe, ["-version"], { encoding: "utf8" });
  const text = `${r.stderr || ""}\n${r.stdout || ""}`;
  const m = text.match(/version\s+"(\d+)/i) || text.match(/version\s+"1\.(\d+)/i);
  if (!m) return 0;
  // "1.8.0" → group is 8 with second regex; "21.0.7" → 21 with first.
  const n = Number(m[1]);
  if (text.includes('version "1.')) return n; // 1.8 → 8
  return n;
}

let javaHome = null;
for (const c of jdkCandidates) {
  const major = javaMajor(c);
  if (major >= 21) {
    javaHome = c;
    break;
  }
  if (c) {
    console.warn(`[assemble-android] skip JDK major=${major || "?"} at ${c}`);
  }
}

if (!javaHome) {
  console.error("[assemble-android] JDK 21+ not found (Capacitor 7 needs 源发行版 21)");
  process.exit(1);
}
if (!fs.existsSync(gradleBat)) {
  console.error("[assemble-android] missing local Gradle 8.11.1 at", gradleBat);
  process.exit(1);
}

const env = {
  ...process.env,
  JAVA_HOME: javaHome,
  ANDROID_HOME:
    process.env.ANDROID_HOME || "D:\\installsoft\\JDK\\android-sdk",
  Path: `${path.join(javaHome, "bin")};${process.env.Path || process.env.PATH}`,
};

console.log("[assemble-android] JAVA_HOME=", javaHome, "major=", javaMajor(javaHome));
console.log("[assemble-android] gradle=", gradleBat);

const r = spawnSync(gradleBat, ["assembleDebug", "--no-daemon"], {
  cwd: androidDir,
  env,
  stdio: "inherit",
  shell: true,
});
process.exit(r.status ?? 1);
