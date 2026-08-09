import { battleSceCandidatesFromWorld } from "../src/resources/battleScene";

function assert(cond: unknown, msg: string): asserts cond {
  if (!cond) throw new Error(msg);
}

const cands = battleSceCandidatesFromWorld("17无情谷_01.sce");
assert(cands[0] === "b_17无情谷.sce", `expect b_17无情谷.sce got ${cands[0]}`);
assert(cands.includes("b_17无情谷m.sce"), "m variant");

const peach = battleSceCandidatesFromWorld("01桃花岛_01.sce");
assert(peach[0] === "b_01桃花岛.sce", peach[0]!);

const already = battleSceCandidatesFromWorld("b_20黑龙潭.sce");
assert(already[0] === "b_20黑龙潭.sce", already[0]!);

console.log("battle-scene resolve OK", { cands, peach });
