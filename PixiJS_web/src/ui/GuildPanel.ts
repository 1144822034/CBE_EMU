/**
 * Guild hub: list / detail / members / applications (CBE softkey lists).
 */
import { Container, Graphics } from "pixi.js";
import {
  CBE,
  LCD_HEIGHT,
  LCD_WIDTH,
  SOFTKEY_H,
  drawCbePanel,
  drawCbeTitlePill,
} from "./constants";
import { BitmapLabel } from "./BitmapLabel";
import type {
  GuildApplicantRow,
  GuildDetail,
  GuildListRow,
  GuildMemberRow,
} from "../net/protocolGuild";

export type GuildAction =
  | "open_list"
  | "open_create"
  | "open_self"
  | "open_members"
  | "open_apps"
  | "leave"
  | "refresh"
  | "detail"
  | "apply"
  | "approve"
  | "reject"
  | "next_page"
  | "prev_page";

type Mode = "hub" | "list" | "members" | "apps" | "actions" | "text";

export class GuildPanel extends Container {
  private box = new Graphics();
  private labels: BitmapLabel[] = [];
  private mode: Mode = "hub";
  private listCursor = 0;
  private actionCursor = 0;
  private title = "帮派";
  private hubLabels: string[] = [];
  private actionLabels: string[] = [];
  private textLines: string[] = [];
  private listRows: GuildListRow[] = [];
  private memberRows: GuildMemberRow[] = [];
  private appRows: GuildApplicantRow[] = [];
  private listPage = 1;
  private listPages = 1;
  private memberPage = 1;
  private memberPages = 1;
  private appPage = 1;
  private appPages = 1;
  private memberMeta = "";
  private appMeta = "";
  private inGuild = false;
  private guildName = "";
  private selectedGuild: GuildListRow | null = null;
  private selectedApp: GuildApplicantRow | null = null;
  private returnMode: Mode = "hub";

  onAction: ((action: GuildAction, payload?: number) => void) | null = null;
  onClose: (() => void) | null = null;

  constructor() {
    super();
    this.visible = false;
    this.addChild(
      new Graphics()
        .rect(0, 0, LCD_WIDTH, LCD_HEIGHT - SOFTKEY_H)
        .fill({ color: 0x000000, alpha: 0.35 }),
    );
    this.addChild(this.box);
  }

  openHub(inGuild: boolean, guildName: string): void {
    this.inGuild = inGuild;
    this.guildName = guildName || (inGuild ? "本帮" : "无帮派");
    this.mode = "hub";
    this.listCursor = 0;
    this.visible = true;
    this.rebuildHub();
    this.redraw();
  }

  setList(rows: GuildListRow[], page: number, allpgs: number, fid: number): void {
    this.listRows = rows;
    this.listPage = page;
    this.listPages = Math.max(1, allpgs);
    if (fid > 0) this.inGuild = true;
    this.mode = "list";
    this.listCursor = 0;
    this.title = `其他帮派 ${page}/${this.listPages}`;
    this.visible = true;
    this.redraw();
  }

  showDetail(detail: GuildDetail, fromList: boolean): void {
    this.selectedGuild = {
      guildId: detail.guildId,
      guildLevel: detail.guildLevel,
      guildName: detail.guildName,
      memberCount: 0,
      leaderName: detail.leaderName,
    };
    this.textLines = [
      `帮主 ${detail.leaderName}`,
      `等级 ${detail.guildLevel}`,
      `人数 ${detail.memberCountText}`,
      `金钱 ${detail.guildMoney}`,
      `繁荣 ${detail.prosperity}`,
      `行动 ${detail.actionPower}`,
      `研究 ${detail.researchPower}`,
      `建设 ${detail.construction}`,
      `工程 ${detail.currentConstruction || "-"}`,
      `公告 ${detail.notice || "(无)"}`,
    ];
    this.title = detail.guildName || "帮派详情";
    this.returnMode = fromList ? "list" : "hub";
    this.mode = "text";
    this.actionLabels =
      fromList && !this.inGuild ? ["申请入帮", "返回"] : ["返回"];
    this.actionCursor = 0;
    this.visible = true;
    this.redraw();
  }

  setMembers(
    members: GuildMemberRow[],
    page: number,
    allpgs: number,
    cnum: number,
    mnum: number,
  ): void {
    this.memberRows = members;
    this.memberPage = page;
    this.memberPages = Math.max(1, allpgs);
    this.memberMeta = `${cnum}/${mnum}`;
    this.mode = "members";
    this.listCursor = 0;
    this.title = `成员 ${page}/${this.memberPages} ${this.memberMeta}`;
    this.visible = true;
    this.redraw();
  }

  setApplications(
    applicants: GuildApplicantRow[],
    page: number,
    allpgs: number,
    result: number,
    roles: number,
    maxroles: number,
  ): void {
    this.appRows = applicants;
    this.appPage = page;
    this.appPages = Math.max(1, allpgs);
    this.appMeta = result === 1 ? `${roles}/${maxroles}` : "";
    this.mode = "apps";
    this.listCursor = 0;
    if (result === 3) this.title = "招收·权限不足";
    else if (result === 4) this.title = "招收·暂无申请";
    else this.title = `招收 ${page}/${this.appPages} ${this.appMeta}`;
    this.visible = true;
    this.redraw();
  }

  get listIndex(): number {
    return this.listPage;
  }
  get memberIndex(): number {
    return this.memberPage;
  }
  get appIndex(): number {
    return this.appPage;
  }

  hide(): void {
    this.visible = false;
  }

  move(delta: number): void {
    if (!this.visible) return;
    if (this.mode === "actions") {
      const n = this.actionLabels.length;
      if (!n) return;
      this.actionCursor = (this.actionCursor + delta + n) % n;
      this.redraw();
      return;
    }
    if (this.mode === "hub") {
      const n = this.hubLabels.length;
      if (!n) return;
      this.listCursor = (this.listCursor + delta + n) % n;
      this.redraw();
      return;
    }
    if (this.mode === "list") {
      const n = Math.max(1, this.listRows.length + 2);
      this.listCursor = (this.listCursor + delta + n) % n;
      this.redraw();
      return;
    }
    if (this.mode === "members") {
      const n = Math.max(1, this.memberRows.length + 2);
      this.listCursor = (this.listCursor + delta + n) % n;
      this.redraw();
      return;
    }
    if (this.mode === "apps") {
      const n = Math.max(1, this.appRows.length + 2);
      this.listCursor = (this.listCursor + delta + n) % n;
      this.redraw();
    }
  }

  confirm(): void {
    if (!this.visible) return;
    if (this.mode === "text") {
      if (this.actionLabels.includes("申请入帮")) {
        this.mode = "actions";
        this.actionCursor = 0;
        this.redraw();
        return;
      }
      this.mode = this.returnMode;
      this.redraw();
      return;
    }
    if (this.mode === "actions") {
      const lab = this.actionLabels[this.actionCursor];
      if (!lab || lab === "返回") {
        this.mode = this.returnMode === "list" ? "list" : this.returnMode === "apps" ? "apps" : "hub";
        this.redraw();
        return;
      }
      if (lab === "申请入帮" && this.selectedGuild) {
        this.onAction?.("apply", this.selectedGuild.guildId);
        return;
      }
      if (lab === "批准" && this.selectedApp) {
        this.onAction?.("approve", this.selectedApp.roleId);
        return;
      }
      if (lab === "拒绝" && this.selectedApp) {
        this.onAction?.("reject", this.selectedApp.roleId);
        return;
      }
      if (lab === "查看详情" && this.selectedGuild) {
        this.onAction?.("detail", this.selectedGuild.guildId);
      }
      return;
    }
    if (this.mode === "hub") {
      const lab = this.hubLabels[this.listCursor];
      if (lab === "其他帮派") this.onAction?.("open_list");
      else if (lab === "创建帮派") this.onAction?.("open_create");
      else if (lab === "本帮情报") this.onAction?.("open_self");
      else if (lab === "成员列表") this.onAction?.("open_members");
      else if (lab === "招收审批") this.onAction?.("open_apps");
      else if (lab === "脱离帮派") this.onAction?.("leave");
      else if (lab === "返回") {
        this.hide();
        this.onClose?.();
      }
      return;
    }
    if (this.mode === "list") {
      const extra = this.listRows.length;
      if (this.listCursor === extra) {
        this.onAction?.("prev_page");
        return;
      }
      if (this.listCursor === extra + 1) {
        this.onAction?.("next_page");
        return;
      }
      const row = this.listRows[this.listCursor];
      if (!row) return;
      this.selectedGuild = row;
      this.returnMode = "list";
      this.actionLabels = this.inGuild
        ? ["查看详情", "返回"]
        : ["查看详情", "申请入帮", "返回"];
      this.actionCursor = 0;
      this.mode = "actions";
      this.redraw();
      return;
    }
    if (this.mode === "members") {
      const extra = this.memberRows.length;
      if (this.listCursor === extra) this.onAction?.("prev_page");
      else if (this.listCursor === extra + 1) this.onAction?.("next_page");
      return;
    }
    if (this.mode === "apps") {
      const extra = this.appRows.length;
      if (this.listCursor === extra) {
        this.onAction?.("prev_page");
        return;
      }
      if (this.listCursor === extra + 1) {
        this.onAction?.("next_page");
        return;
      }
      const row = this.appRows[this.listCursor];
      if (!row) return;
      this.selectedApp = row;
      this.returnMode = "apps";
      this.actionLabels = ["批准", "拒绝", "返回"];
      this.actionCursor = 0;
      this.mode = "actions";
      this.redraw();
    }
  }

  cancel(): void {
    if (!this.visible) return;
    if (this.mode === "actions" || this.mode === "text") {
      this.mode = this.returnMode;
      if (this.mode === "hub") this.rebuildHub();
      this.redraw();
      return;
    }
    if (this.mode === "list" || this.mode === "members" || this.mode === "apps") {
      this.mode = "hub";
      this.rebuildHub();
      this.redraw();
      return;
    }
    this.hide();
    this.onClose?.();
  }

  private rebuildHub(): void {
    this.title = this.inGuild ? `帮派·${this.guildName}` : "帮派";
    if (this.inGuild) {
      this.hubLabels = [
        "本帮情报",
        "成员列表",
        "招收审批",
        "脱离帮派",
        "其他帮派",
        "返回",
      ];
    } else {
      this.hubLabels = ["其他帮派", "创建帮派", "返回"];
    }
  }

  private redraw(): void {
    for (const t of this.labels) t.destroy();
    this.labels = [];
    this.box.clear();
    const x = 8;
    const y = 28;
    const w = LCD_WIDTH - 16;
    const h = LCD_HEIGHT - SOFTKEY_H - 40;
    drawCbePanel(this.box, x, y, w, h, CBE.bgDeep);
    const pillW = Math.min(w - 20, Math.max(80, this.title.length * 12 + 20));
    drawCbeTitlePill(this.box, LCD_WIDTH / 2, y + 6, pillW, 20);
    this.addLabel(
      this.title.slice(0, 14),
      LCD_WIDTH / 2 - pillW / 2 + 8,
      y + 9,
      CBE.ink,
    );

    if (this.mode === "text") {
      this.textLines.slice(0, 12).forEach((line, i) => {
        this.addLabel(line.slice(0, 28), x + 10, y + 36 + i * 15, CBE.inkMint);
      });
      this.addLabel(
        this.actionLabels.includes("申请入帮") ? "确定=操作" : "确定=返回",
        x + 10,
        y + h - 18,
        CBE.inkLabel,
      );
      return;
    }

    if (this.mode === "actions") {
      this.actionLabels.forEach((lab, i) => {
        const hi = i === this.actionCursor;
        if (hi) {
          this.box
            .rect(x + 6, y + 36 + i * 18, w - 12, 16)
            .fill({ color: CBE.select });
        }
        this.addLabel(lab, x + 12, y + 38 + i * 18, hi ? CBE.ink : CBE.inkMint);
      });
      return;
    }

    if (this.mode === "hub") {
      this.hubLabels.forEach((lab, i) => {
        const hi = i === this.listCursor;
        if (hi) {
          this.box
            .rect(x + 6, y + 36 + i * 18, w - 12, 16)
            .fill({ color: CBE.select });
        }
        this.addLabel(lab, x + 12, y + 38 + i * 18, hi ? CBE.ink : CBE.inkMint);
      });
      return;
    }

    if (this.mode === "list") {
      if (!this.listRows.length) {
        this.addLabel("(无帮派)", x + 10, y + 40, CBE.inkMint);
      }
      this.listRows.slice(0, 10).forEach((row, i) => {
        const hi = i === this.listCursor;
        if (hi) {
          this.box
            .rect(x + 6, y + 34 + i * 18, w - 12, 16)
            .fill({ color: CBE.select });
        }
        this.addLabel(
          `${row.guildName} Lv${row.guildLevel}`.slice(0, 14),
          x + 10,
          y + 36 + i * 18,
          hi ? CBE.ink : CBE.inkMint,
        );
        this.addLabel(`${row.memberCount}人`, x + 140, y + 36 + i * 18, CBE.inkLabel);
      });
      this.drawPager(x, y, w, this.listRows.length, this.listPage, this.listPages);
      return;
    }

    if (this.mode === "members") {
      if (!this.memberRows.length) {
        this.addLabel("(无成员)", x + 10, y + 40, CBE.inkMint);
      }
      this.memberRows.slice(0, 10).forEach((row, i) => {
        const hi = i === this.listCursor;
        if (hi) {
          this.box
            .rect(x + 6, y + 34 + i * 18, w - 12, 16)
            .fill({ color: CBE.select });
        }
        const on = row.online ? "" : "离";
        this.addLabel(
          `${row.roleName}${on}`.slice(0, 10),
          x + 10,
          y + 36 + i * 18,
          hi ? CBE.ink : CBE.inkMint,
        );
        this.addLabel(
          `${row.title} Lv${row.level}`.slice(0, 12),
          x + 100,
          y + 36 + i * 18,
          CBE.inkLabel,
        );
      });
      this.drawPager(
        x,
        y,
        w,
        this.memberRows.length,
        this.memberPage,
        this.memberPages,
      );
      return;
    }

    if (this.mode === "apps") {
      if (!this.appRows.length) {
        this.addLabel("(无申请)", x + 10, y + 40, CBE.inkMint);
      }
      this.appRows.slice(0, 10).forEach((row, i) => {
        const hi = i === this.listCursor;
        if (hi) {
          this.box
            .rect(x + 6, y + 34 + i * 18, w - 12, 16)
            .fill({ color: CBE.select });
        }
        this.addLabel(
          row.roleName.slice(0, 12),
          x + 10,
          y + 36 + i * 18,
          hi ? CBE.ink : CBE.inkMint,
        );
        this.addLabel(`Lv${row.level}`, x + 140, y + 36 + i * 18, CBE.inkLabel);
      });
      this.drawPager(x, y, w, this.appRows.length, this.appPage, this.appPages);
    }
  }

  private drawPager(
    x: number,
    y: number,
    w: number,
    rowCount: number,
    page: number,
    pages: number,
  ): void {
    const base = y + 34 + Math.min(rowCount, 10) * 18;
    const items = [`上一页(${page}/${pages})`, "下一页"];
    items.forEach((lab, i) => {
      const idx = rowCount + i;
      const hi = idx === this.listCursor;
      if (hi) {
        this.box.rect(x + 6, base + i * 18, w - 12, 16).fill({ color: CBE.select });
      }
      this.addLabel(lab, x + 12, base + 2 + i * 18, hi ? CBE.ink : CBE.inkMint);
    });
  }

  private addLabel(text: string, lx: number, ly: number, color: number): void {
    const t = new BitmapLabel();
    void t.setText(text, color);
    t.position.set(lx, ly);
    this.addChild(t);
    this.labels.push(t);
  }
}
