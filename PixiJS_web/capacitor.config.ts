import type { CapacitorConfig } from "@capacitor/cli";

const config: CapacitorConfig = {
  appId: "com.jianghu.pixijs",
  appName: "江湖OL",
  webDir: "dist",
  server: {
    androidScheme: "https",
    cleartext: true,
    /** Shown when System WebView is below android.minWebViewVersion. */
    errorPath: "webview-update.html",
  },
  android: {
    allowMixedContent: true,
    /**
     * Capacitor default floor is 60. 69 blocked too many OEM / China-ROM
     * devices whose package versionName parses low or lags Play updates.
     * Truly ancient WebViews still hit this page; others can tap through.
     */
    minWebViewVersion: 60,
  },
  plugins: {
    /** Native HTTP so entertainment video resolve can follow redirects without CORS. */
    CapacitorHttp: {
      enabled: true,
    },
  },
};

export default config;
