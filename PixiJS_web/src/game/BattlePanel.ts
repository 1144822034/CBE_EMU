import type { BattleState } from "../net/protocol";

export class BattlePanel {
  readonly el: HTMLDivElement;
  private onAttack: (() => void) | null = null;
  private onFlee: (() => void) | null = null;

  constructor(parent: HTMLElement) {
    this.el = document.createElement("div");
    this.el.id = "battle-panel";
    this.el.hidden = true;
    this.el.innerHTML = `
      <div class="battle-card">
        <div class="battle-title">战斗</div>
        <div class="battle-rows" id="battle-rows"></div>
        <div class="battle-actions">
          <button type="button" id="battle-atk">攻击</button>
          <button type="button" id="battle-flee">逃跑</button>
        </div>
        <div class="battle-log" id="battle-log"></div>
      </div>
    `;
    parent.appendChild(this.el);
    this.el.querySelector("#battle-atk")!.addEventListener("click", () => {
      this.log("发动普通攻击（演示）");
      this.onAttack?.();
    });
    this.el.querySelector("#battle-flee")!.addEventListener("click", () => {
      this.hide();
      this.onFlee?.();
    });
  }

  setHandlers(attack: () => void, flee: () => void): void {
    this.onAttack = attack;
    this.onFlee = flee;
  }

  show(state: BattleState): void {
    this.el.hidden = false;
    const rows = this.el.querySelector("#battle-rows")!;
    const sideLabel = (s: { name?: string; hp: number; maxHp: number; id: number }) =>
      `<div class="battle-row"><span>${s.name ?? "#" + s.id}</span><span>${s.hp}/${s.maxHp}</span></div>`;
    rows.innerHTML = `
      <div class="battle-col"><div class="battle-h">敌方</div>${state.left.map(sideLabel).join("")}</div>
      <div class="battle-col"><div class="battle-h">我方</div>${state.right.map(sideLabel).join("")}</div>
    `;
    this.log(
      `开战 subtype=${state.subtype} side=${state.side}` +
        (state.sceneMonster
          ? ` 场景怪 index=${state.sceneMonster.index}`
          : ""),
    );
  }

  hide(): void {
    this.el.hidden = true;
  }

  get visible(): boolean {
    return !this.el.hidden;
  }

  private log(msg: string): void {
    const log = this.el.querySelector("#battle-log");
    if (log) log.textContent = msg;
  }
}
