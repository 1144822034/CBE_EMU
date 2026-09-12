import { FIXED_LIVE_GATEWAY } from "./Transport";

export const LS_ZONE_ID = "pix_zone_id";
export const LS_GATEWAY = "pix_gateway";

export type GatewayZoneRow = {
  zoneId: number;
  name: string;
  label: string;
  wsUrl: string;
  accountWebUrl: string;
};

export type GatewayZoneCatalog = {
  ok: boolean;
  defaultZoneId: number;
  zones: GatewayZoneRow[];
  error?: string;
  sourceUrl?: string;
};

/** Mirror Transport.defaultLiveGatewayUrl without importing it (cycle). */
function liveGatewayHint(): string {
  if (typeof window === "undefined") return FIXED_LIVE_GATEWAY;
  const params = new URLSearchParams(window.location.search);
  const fromQuery = params.get("gateway") || params.get("ws");
  if (fromQuery) return fromQuery;
  try {
    const stored = window.localStorage.getItem("pix_gateway");
    if (stored) return stored;
  } catch {
    /* private mode */
  }
  return FIXED_LIVE_GATEWAY;
}

function wsToZonesHttp(wsUrl: string): string | null {
  try {
    const u = new URL(wsUrl);
    const httpProto = u.protocol === "wss:" ? "https:" : "http:";
    return `${httpProto}//${u.host}/api/zones`;
  } catch {
    return null;
  }
}

function normalizeZonesUrl(raw: string): string {
  const s = raw.trim();
  if (!s) return s;
  if (s.includes("/api/zones")) return s;
  return s.replace(/\/+$/, "") + "/api/zones";
}

/**
 * Catalog URL candidates. Prefer bootstrap FIXED, then current live gateway,
 * then local Vite/dev hosts — first successful response wins.
 */
export function zoneCatalogUrlCandidates(): string[] {
  const out: string[] = [];
  const push = (url: string | null | undefined) => {
    if (!url) return;
    const n = normalizeZonesUrl(url);
    if (!n || out.includes(n)) return;
    out.push(n);
  };

  if (typeof window !== "undefined") {
    const params = new URLSearchParams(window.location.search);
    push(params.get("zones") || params.get("zoneCatalog") || undefined);
    try {
      push(window.localStorage.getItem("pix_zone_catalog") || undefined);
    } catch {
      /* private mode */
    }
  }

  push(wsToZonesHttp(FIXED_LIVE_GATEWAY));
  push(wsToZonesHttp(liveGatewayHint()));

  if (typeof window !== "undefined") {
    const host = window.location.hostname;
    if (host === "localhost" || host === "127.0.0.1") {
      push("http://127.0.0.1:19092/api/zones");
    }
    if (host && host !== "localhost" && host !== "127.0.0.1") {
      push(`http://${host}:19092/api/zones`);
    }
  }

  if (!out.length) push("http://127.0.0.1:19092/api/zones");
  return out;
}

/** Primary catalog URL (first candidate). */
export function defaultZoneCatalogUrl(): string {
  return zoneCatalogUrlCandidates()[0] || "http://127.0.0.1:19092/api/zones";
}

function parseZoneRow(raw: unknown): GatewayZoneRow | null {
  if (!raw || typeof raw !== "object") return null;
  const o = raw as Record<string, unknown>;
  const zoneId = Number(o.zoneId);
  const wsUrl = typeof o.wsUrl === "string" ? o.wsUrl.trim() : "";
  if (!Number.isFinite(zoneId) || zoneId <= 0) return null;
  if (!wsUrl.startsWith("ws://") && !wsUrl.startsWith("wss://")) return null;
  return {
    zoneId: zoneId >>> 0,
    name: typeof o.name === "string" ? o.name : `区服${zoneId}`,
    label: typeof o.label === "string" ? o.label : "",
    wsUrl,
    accountWebUrl: typeof o.accountWebUrl === "string" ? o.accountWebUrl : "",
  };
}

async function fetchZoneCatalogOnce(url: string): Promise<GatewayZoneCatalog> {
  try {
    const res = await fetch(url, { method: "GET", cache: "no-store" });
    if (!res.ok) {
      return {
        ok: false,
        defaultZoneId: 0,
        zones: [],
        error: `http-${res.status}`,
        sourceUrl: url,
      };
    }
    const data = (await res.json()) as Record<string, unknown>;
    const list = Array.isArray(data.zones) ? data.zones : [];
    const zones: GatewayZoneRow[] = [];
    for (const item of list) {
      const row = parseZoneRow(item);
      if (row) zones.push(row);
    }
    const defaultZoneId = Number(data.defaultZoneId);
    return {
      ok: data.ok === true || zones.length > 0,
      defaultZoneId:
        Number.isFinite(defaultZoneId) && defaultZoneId > 0
          ? defaultZoneId >>> 0
          : zones[0]?.zoneId ?? 0,
      zones,
      sourceUrl: url,
      error: zones.length ? undefined : "empty-catalog",
    };
  } catch (e) {
    const msg = e instanceof Error ? e.message : String(e);
    return {
      ok: false,
      defaultZoneId: 0,
      zones: [],
      error: msg,
      sourceUrl: url,
    };
  }
}

export async function loadGatewayZoneCatalog(
  urlOrUrls?: string | string[],
): Promise<GatewayZoneCatalog> {
  const urls = Array.isArray(urlOrUrls)
    ? urlOrUrls
    : urlOrUrls
      ? [urlOrUrls]
      : zoneCatalogUrlCandidates();
  let last: GatewayZoneCatalog = {
    ok: false,
    defaultZoneId: 0,
    zones: [],
    error: "no-url",
  };
  for (const url of urls) {
    last = await fetchZoneCatalogOnce(url);
    if (last.zones.length > 0) return last;
  }
  return last;
}

export function readStoredZoneId(): number {
  if (typeof window === "undefined") return 0;
  try {
    const raw = window.localStorage.getItem(LS_ZONE_ID);
    const id = raw ? Number(raw) : 0;
    return Number.isFinite(id) && id > 0 ? id >>> 0 : 0;
  } catch {
    return 0;
  }
}

/** Persist zone + gateway URL for LiveTransport / register / recharge. */
export function applyGatewayZone(zone: GatewayZoneRow): void {
  if (typeof window === "undefined") return;
  try {
    window.localStorage.setItem(LS_ZONE_ID, String(zone.zoneId));
    window.localStorage.setItem(LS_GATEWAY, zone.wsUrl);
  } catch {
    /* private mode */
  }
}

/**
 * Ensure localStorage has a gateway when catalog is available.
 * - 0 zones: leave storage alone (FIXED_LIVE_GATEWAY fallback)
 * - 1 zone: silent apply
 * - multi: apply default only if no stored zone id
 * Returns whether the UI must force a zone picker.
 */
export function resolveGatewayZoneSelection(
  catalog: GatewayZoneCatalog,
): { forcePicker: boolean; applied: GatewayZoneRow | null } {
  const zones = catalog.zones;
  if (zones.length === 0) {
    return { forcePicker: false, applied: null };
  }
  if (zones.length === 1) {
    applyGatewayZone(zones[0]!);
    return { forcePicker: false, applied: zones[0]! };
  }
  const storedId = readStoredZoneId();
  const match = storedId ? zones.find((z) => z.zoneId === storedId) : undefined;
  if (match) {
    applyGatewayZone(match);
    return { forcePicker: false, applied: match };
  }
  const def =
    zones.find((z) => z.zoneId === catalog.defaultZoneId) ?? zones[0] ?? null;
  return { forcePicker: true, applied: def };
}
