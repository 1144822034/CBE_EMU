/**
 * Public asset URLs under Vite `base: './'`.
 * Leading `/…` breaks Electron `file://` (resolves to the disk root).
 */
export function publicAssetUrl(assetPath: string): string {
  const clean = String(assetPath || "").replace(/^\/+/, "");
  if (!clean) return "./";
  if (typeof document !== "undefined" && document.baseURI) {
    try {
      return new URL(clean, document.baseURI).href;
    } catch {
      /* fall through */
    }
  }
  return `./${clean}`;
}

/** JHOnlineData/<name> under the bundled public root. */
export function jhOnlineDataUrl(name: string): string {
  return publicAssetUrl(`JHOnlineData/${encodeURIComponent(name)}`);
}
