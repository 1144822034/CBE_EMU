/**
 * Live NPC dialog: open type=1, parse dialog blob, exercise task/service options.
 */
import WebSocket from "ws";
import {
  CBMR_FLAG_HAS_FOLLOWUP,
  decodeCbmr,
  encodeCbms,
  encodeRequestMeta,
} from "../src/net/cbms";
import {
  buildGroupType1Request,
  buildLoginAlt12Request,
  buildRoleSelectRequest,
  buildSceneStartupFollowupRequest,
  buildServerSelectRequest,
  ingestPacket,
  parseNpcCatalog,
  parseRoleListFromSelect,
  parseRoleSelectEnter,
  parseSceneEnter,
  parseServerList,
} from "../src/net/protocol";
import {
  buildChallengeConfirmRequest,
  buildNpcDialogRequest,
  buildTaskAcceptRequest,
  buildTaskDetailRequest,
  npcServiceOpcode,
  parseChallengePrompt,
  parseNpcDialog,
  parseServerSceneLanding,
  parseTaskDetail,
  NpcServiceOpcode,
} from "../src/net/protocolExtras";

const url = process.env.PIX_GATEWAY || "ws://127.0.0.1:19092";
const user = process.env.PIX_USER || "lxh001";
const pass = process.env.PIX_PASS || "123456";
const clientId = (Math.random() * 0xffffff) >>> 0;

function exchange(ws: WebSocket, wt: Uint8Array, timeoutMs = 12000): Promise<Uint8Array[]> {
  return new Promise((resolve, reject) => {
    const bodies: Uint8Array[] = [];
    const timer = setTimeout(() => {
      cleanup();
      bodies.length ? resolve(bodies) : reject(new Error("timeout"));
    }, timeoutMs);
    const onMsg = (data: WebSocket.RawData) => {
      const buf =
        data instanceof Buffer ? new Uint8Array(data) : new Uint8Array(data as ArrayBuffer);
      try {
        const frame = decodeCbmr(buf);
        if (frame.body.length) bodies.push(frame.body);
        if ((frame.flags & CBMR_FLAG_HAS_FOLLOWUP) === 0) {
          cleanup();
          resolve(bodies);
        }
      } catch (e) {
        cleanup();
        reject(e);
      }
    };
    const cleanup = () => {
      clearTimeout(timer);
      ws.off("message", onMsg);
    };
    ws.on("message", onMsg);
    ws.send(Buffer.from(encodeCbms(0, encodeRequestMeta(clientId), wt)));
  });
}

function dump(label: string, bodies: Uint8Array[]) {
  console.log(`\n=== ${label} bodies=${bodies.length} ===`);
  for (const [i, b] of bodies.entries()) {
    const p = ingestPacket(b);
    if (!p) continue;
    console.log(i, p.objects.map((o) => `${o.kind}/${o.subtype}`).join(", "));
    const d = parseNpcDialog(p);
    if (d) {
      console.log("  DIALOG", d.body.slice(0, 40), "opts", d.options.map((o) => `${o.label}:a${o.action}:v${o.value >>> 0}`));
    }
    const detail = parseTaskDetail(p);
    if (detail) console.log("  TASK_DETAIL", detail.slice(0, 60));
    const ch = parseChallengePrompt(p);
    if (ch) console.log("  CHALLENGE", ch.text.slice(0, 60));
    const land = parseServerSceneLanding(p);
    if (land) console.log("  LANDING", land);
  }
}

const ws = new WebSocket(url);
await new Promise<void>((res, rej) => {
  ws.once("open", () => res());
  ws.once("error", rej);
});

const login = await exchange(ws, buildLoginAlt12Request(user, pass));
let servers = parseServerList(ingestPacket(login[0]!)!);
for (const b of login) {
  const p = ingestPacket(b);
  if (!p) continue;
  const list = parseServerList(p);
  if (list.servers.length) servers = list;
}
if (!servers.servers.length) throw new Error("no servers: " + (servers.information || ""));
const rolesB = await exchange(ws, buildServerSelectRequest(servers.servers[0]!.serverId));
let roles = parseRoleListFromSelect(ingestPacket(rolesB[0]!)!);
for (const b of rolesB) {
  const p = ingestPacket(b);
  if (!p) continue;
  const r = parseRoleListFromSelect(p);
  if (r.length) roles = r;
}
const enterB = await exchange(ws, buildRoleSelectRequest(roles[0]!.roleId));
await exchange(ws, buildGroupType1Request());
const follow = await exchange(ws, buildSceneStartupFollowupRequest());
let enter = null as ReturnType<typeof parseSceneEnter>;
let npcs = [] as ReturnType<typeof parseNpcCatalog>;
for (const b of [...enterB, ...follow]) {
  const p = ingestPacket(b);
  if (!p) continue;
  enter = parseSceneEnter(p) || parseRoleSelectEnter(p) || enter;
  const n = parseNpcCatalog(p);
  if (n.length) npcs = n;
}
if (!enter?.scene) throw new Error("no enter");
console.log("ENTER", enter.scene, `@${enter.x},${enter.y}`, "npc", npcs.length);
if (!npcs.length) throw new Error("no NPCs in catalog — need a map with dynamic NPC");

const npc = npcs[0]!;
console.log("tap NPC", npc.actorId, npc.name);
const root = await exchange(ws, buildNpcDialogRequest(npc.actorId, 1));
dump("root 26/1 type=1", root);
let dialog = null as ReturnType<typeof parseNpcDialog>;
for (const b of root) {
  const p = ingestPacket(b);
  if (!p) continue;
  dialog = parseNpcDialog(p) || dialog;
}
if (!dialog) throw new Error("no dialog parse");

const taskOpt = dialog.options.find((o) => o.action === 4);
const svcOpt = dialog.options.find((o) => o.action === 1);
const challengeOpt = dialog.options.find(
  (o) => o.action === 1 && npcServiceOpcode(o.value) === NpcServiceOpcode.CHALLENGE,
);
const teleportOpt = dialog.options.find(
  (o) => o.action === 1 && npcServiceOpcode(o.value) === NpcServiceOpcode.ENTER_INSTANCE,
);

if (taskOpt) {
  const detail = await exchange(ws, buildTaskDetailRequest(taskOpt.value, 0));
  dump("task 6/10", detail);
  if (!/提交/.test(taskOpt.label)) {
    const acc = await exchange(ws, buildTaskAcceptRequest(taskOpt.value));
    dump("task 6/11 accept", acc);
  }
}

if (teleportOpt) {
  const tp = await exchange(ws, buildNpcDialogRequest(teleportOpt.value, 2));
  dump("teleport EB type=2", tp);
}

if (challengeOpt) {
  const ch = await exchange(ws, buildNpcDialogRequest(challengeOpt.value, 2));
  dump("challenge EC type=2", ch);
  let hasPrompt = false;
  for (const b of ch) {
    const p = ingestPacket(b);
    if (p && parseChallengePrompt(p)) hasPrompt = true;
  }
  if (hasPrompt) {
    const conf = await exchange(ws, buildChallengeConfirmRequest(true));
    dump("challenge 30/10", conf);
  }
} else if (svcOpt && !teleportOpt) {
  const svc = await exchange(ws, buildNpcDialogRequest(svcOpt.value, 2));
  dump("service type=2", svc);
}

ws.close();
console.log("\nDONE");
