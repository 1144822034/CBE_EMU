/**
 * Re-apply Capacitor Bridge null-guards for Android 8.0 WebView package probes.
 * Safe to run repeatedly (idempotent).
 */
import fs from "node:fs";
import path from "node:path";
import { fileURLToPath } from "node:url";

const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), "..");
const bridgePath = path.join(
  root,
  "node_modules",
  "@capacitor",
  "android",
  "capacitor",
  "src",
  "main",
  "java",
  "com",
  "getcapacitor",
  "Bridge.java",
);

const MARKER = "Some Android 8.0 OEMs return null PackageInfo";

const needle = [
  "        // Check getCurrentWebViewPackage() directly if above Android 8",
  "        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O) {",
  "            PackageInfo info = WebView.getCurrentWebViewPackage();",
  '            Pattern pattern = Pattern.compile("(\\d+)");',
  "            Matcher matcher = pattern.matcher(info.versionName);",
  "            if (matcher.find()) {",
  "                String majorVersionStr = matcher.group(0);",
  "                int majorVersion = Integer.parseInt(majorVersionStr);",
  '                if (info.packageName.equals("com.huawei.webview")) {',
  "                    return majorVersion >= config.getMinHuaweiWebViewVersion();",
  "                }",
  "                return majorVersion >= config.getMinWebViewVersion();",
  "            } else {",
  "                return false;",
  "            }",
  "        }",
].join("\n");

const replacement = [
  "        // Check getCurrentWebViewPackage() directly if above Android 8.",
  "        // Some Android 8.0 OEMs return null PackageInfo / versionName → NPE.",
  "        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O) {",
  "            PackageInfo info = WebView.getCurrentWebViewPackage();",
  "            if (info != null && info.versionName != null) {",
  '                Pattern pattern = Pattern.compile("(\\d+)");',
  "                Matcher matcher = pattern.matcher(info.versionName);",
  "                if (matcher.find()) {",
  "                    String majorVersionStr = matcher.group(0);",
  "                    int majorVersion = Integer.parseInt(majorVersionStr);",
  '                    if (info.packageName != null && info.packageName.equals("com.huawei.webview")) {',
  "                        return majorVersion >= config.getMinHuaweiWebViewVersion();",
  "                    }",
  "                    return majorVersion >= config.getMinWebViewVersion();",
  "                }",
  "            }",
  "            // Fall through to package-manager probes below.",
  "        }",
].join("\n");

if (!fs.existsSync(bridgePath)) {
  console.warn("[patch-capacitor-android8] Bridge.java missing — skip");
  process.exit(0);
}

let src = fs.readFileSync(bridgePath, "utf8");
if (src.includes(MARKER)) {
  console.log("[patch-capacitor-android8] already applied");
  process.exit(0);
}
if (!src.includes(needle)) {
  console.warn(
    "[patch-capacitor-android8] unexpected Bridge.java shape — skip (manual check)",
  );
  process.exit(0);
}
src = src.replace(needle, replacement);
fs.writeFileSync(bridgePath, src);
console.log("[patch-capacitor-android8] patched Bridge.java");
