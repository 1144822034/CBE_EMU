/*
 * Pixi ladder (WT kind=44): daily 3-job PvE tower. Included after world_boss.c.
 */

enum
{
    VM_MOCK_LADDER_WT_KIND = 44,
    VM_MOCK_LADDER_WT_STATUS = 1,
    VM_MOCK_LADDER_WT_START = 2,
    VM_MOCK_LADDER_REWARD_MAX = 64,
    VM_MOCK_LADDER_JOB_COUNT = 3
};

typedef struct
{
    u8 enabled;
    u16 dailyLimit;
    u32 enemyIdTianji;
    u32 enemyIdHuanjian;
    u32 enemyIdGuidao;
    u32 baseHp;
    u32 baseAtk;
    u32 baseDef;
    u32 baseMp;
    u32 hpPerFloor;
    u32 atkPerFloor;
    u32 defPerFloor;
    u16 damageScalePerFloor;
    u8 castSkill;
} vm_mock_ladder_config;

typedef struct
{
    u16 floorMilestone;
    u32 rewardExp;
    u32 rewardMoney;
    u32 rewardItemId;
    u32 rewardItemCount;
} vm_mock_ladder_reward;

static vm_mock_ladder_config g_vm_mock_ladder_config;
static bool g_vm_mock_ladder_config_loaded = false;
static vm_mock_ladder_reward g_vm_mock_ladder_rewards[VM_MOCK_LADDER_REWARD_MAX];
static u32 g_vm_mock_ladder_reward_count = 0;

/*
 * Per-role ladder arming.  Process-global armed/floor was cleared by ANY
 * client's battle end (death or victory), so concurrent players often wiped a
 * climber's settle before note_victory — best_floor never advanced.
 */
enum { VM_MOCK_LADDER_ACTIVE_MAX = 32 };

typedef struct
{
    u32 roleId;
    u16 floor;
    u8 armed;
    u32 enemyIds[VM_MOCK_LADDER_JOB_COUNT];
    u32 hp[VM_MOCK_LADDER_JOB_COUNT];
    u32 mp[VM_MOCK_LADDER_JOB_COUNT];
} vm_mock_ladder_battle_slot;

static vm_mock_ladder_battle_slot
    g_vm_mock_ladder_battles[VM_MOCK_LADDER_ACTIVE_MAX];

static vm_mock_ladder_battle_slot *vm_mock_ladder_battle_find(u32 roleId)
{
    u32 i;

    if (roleId == 0)
        return NULL;
    for (i = 0; i < VM_MOCK_LADDER_ACTIVE_MAX; ++i)
    {
        if (g_vm_mock_ladder_battles[i].armed &&
            g_vm_mock_ladder_battles[i].roleId == roleId)
            return &g_vm_mock_ladder_battles[i];
    }
    return NULL;
}

static vm_mock_ladder_battle_slot *vm_mock_ladder_battle_alloc(u32 roleId)
{
    u32 i;
    vm_mock_ladder_battle_slot *slot = NULL;

    if (roleId == 0)
        return NULL;
    slot = vm_mock_ladder_battle_find(roleId);
    if (slot != NULL)
        return slot;
    for (i = 0; i < VM_MOCK_LADDER_ACTIVE_MAX; ++i)
    {
        if (!g_vm_mock_ladder_battles[i].armed)
        {
            memset(&g_vm_mock_ladder_battles[i], 0,
                   sizeof(g_vm_mock_ladder_battles[i]));
            g_vm_mock_ladder_battles[i].roleId = roleId;
            return &g_vm_mock_ladder_battles[i];
        }
    }
    return NULL;
}

static vm_mock_ladder_battle_slot *vm_mock_ladder_battle_active_slot(void)
{
    vm_net_mock_role_state *role = vm_net_mock_active_role();

    if (role == NULL)
        return NULL;
    return vm_mock_ladder_battle_find(role->roleId);
}

static bool vm_mock_ladder_ensure_tables(void)
{
    static bool ready = false;

    if (ready)
        return true;
    if (!vm_mysql_exec(
            "CREATE TABLE IF NOT EXISTS server_ladder_config ("
            "id TINYINT UNSIGNED NOT NULL DEFAULT 1,"
            "enabled TINYINT UNSIGNED NOT NULL DEFAULT 1,"
            "daily_limit SMALLINT UNSIGNED NOT NULL DEFAULT 1,"
            "enemy_id_tianji INT UNSIGNED NOT NULL DEFAULT 920001,"
            "enemy_id_huanjian INT UNSIGNED NOT NULL DEFAULT 920002,"
            "enemy_id_guidao INT UNSIGNED NOT NULL DEFAULT 920003,"
            "base_hp INT UNSIGNED NOT NULL DEFAULT 800,"
            "base_atk INT UNSIGNED NOT NULL DEFAULT 120,"
            "base_def INT UNSIGNED NOT NULL DEFAULT 80,"
            "base_mp INT UNSIGNED NOT NULL DEFAULT 200,"
            "hp_per_floor INT UNSIGNED NOT NULL DEFAULT 150,"
            "atk_per_floor INT UNSIGNED NOT NULL DEFAULT 25,"
            "def_per_floor INT UNSIGNED NOT NULL DEFAULT 15,"
            "damage_scale_per_floor SMALLINT UNSIGNED NOT NULL DEFAULT 5,"
            "cast_skill TINYINT UNSIGNED NOT NULL DEFAULT 1,"
            "updated_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP "
            "ON UPDATE CURRENT_TIMESTAMP,PRIMARY KEY (id)) ENGINE=InnoDB") ||
        !vm_mysql_exec(
            "CREATE TABLE IF NOT EXISTS server_ladder_rewards ("
            "floor_milestone SMALLINT UNSIGNED NOT NULL,"
            "reward_exp INT UNSIGNED NOT NULL DEFAULT 0,"
            "reward_money INT UNSIGNED NOT NULL DEFAULT 0,"
            "reward_item_id INT UNSIGNED NOT NULL DEFAULT 0,"
            "reward_item_count INT UNSIGNED NOT NULL DEFAULT 0,"
            "updated_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP "
            "ON UPDATE CURRENT_TIMESTAMP,PRIMARY KEY (floor_milestone)) "
            "ENGINE=InnoDB") ||
        !vm_mysql_exec(
            "CREATE TABLE IF NOT EXISTS server_ladder_progress ("
            "account_id VARBINARY(63) NOT NULL,"
            "role_id INT UNSIGNED NOT NULL,"
            "best_floor SMALLINT UNSIGNED NOT NULL DEFAULT 0,"
            "updated_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP "
            "ON UPDATE CURRENT_TIMESTAMP,"
            "PRIMARY KEY (account_id,role_id)) ENGINE=InnoDB") ||
        !vm_mysql_exec(
            "CREATE TABLE IF NOT EXISTS server_ladder_daily ("
            "account_id VARBINARY(63) NOT NULL,"
            "role_id INT UNSIGNED NOT NULL,"
            "day_ymd INT UNSIGNED NOT NULL,"
            "attempt_count SMALLINT UNSIGNED NOT NULL DEFAULT 0,"
            "best_floor SMALLINT UNSIGNED NOT NULL DEFAULT 0,"
            "updated_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP "
            "ON UPDATE CURRENT_TIMESTAMP,"
            "PRIMARY KEY (account_id,role_id,day_ymd)) ENGINE=InnoDB") ||
        !vm_mysql_exec(
            /* Daily first clear of each 10x floor (world announce gate). */
            "CREATE TABLE IF NOT EXISTS server_ladder_daily_lead ("
            "day_ymd INT UNSIGNED NOT NULL,"
            "floor_n SMALLINT UNSIGNED NOT NULL,"
            "role_id INT UNSIGNED NOT NULL,"
            "created_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,"
            "PRIMARY KEY (day_ymd,floor_n)) ENGINE=InnoDB"))
        return false;
    /* Existing installs: add columns introduced after first ladder rollout. */
    (void)vm_mysql_exec(
        "ALTER TABLE server_ladder_config ADD COLUMN "
        "damage_scale_per_floor SMALLINT UNSIGNED NOT NULL DEFAULT 5 "
        "AFTER def_per_floor");
    (void)vm_mysql_exec(
        "ALTER TABLE server_ladder_config ADD COLUMN "
        "cast_skill TINYINT UNSIGNED NOT NULL DEFAULT 1 "
        "AFTER damage_scale_per_floor");
    (void)vm_mysql_exec(
        "ALTER TABLE server_ladder_daily ADD COLUMN "
        "best_floor SMALLINT UNSIGNED NOT NULL DEFAULT 0 "
        "AFTER attempt_count");
    ready = true;
    return true;
}

static bool vm_mock_ladder_config_row(void *contextValue, unsigned int columnCount,
                                      const char *const *values, const size_t *lengths)
{
    vm_mock_ladder_config *cfg = (vm_mock_ladder_config *)contextValue;
    u32 tmp = 0;

    (void)columnCount;
    if (cfg == NULL || values == NULL || lengths == NULL)
        return false;
    if (values[0] && lengths[0])
        cfg->enabled = (u8)(values[0][0] == '0' ? 0 : 1);
    if (values[1] && vm_mock_mysql_parse_u32(values[1], lengths[1], &tmp))
        cfg->dailyLimit = (u16)tmp;
    if (values[2] && vm_mock_mysql_parse_u32(values[2], lengths[2], &tmp))
        cfg->enemyIdTianji = tmp;
    if (values[3] && vm_mock_mysql_parse_u32(values[3], lengths[3], &tmp))
        cfg->enemyIdHuanjian = tmp;
    if (values[4] && vm_mock_mysql_parse_u32(values[4], lengths[4], &tmp))
        cfg->enemyIdGuidao = tmp;
    if (values[5] && vm_mock_mysql_parse_u32(values[5], lengths[5], &tmp))
        cfg->baseHp = tmp;
    if (values[6] && vm_mock_mysql_parse_u32(values[6], lengths[6], &tmp))
        cfg->baseAtk = tmp;
    if (values[7] && vm_mock_mysql_parse_u32(values[7], lengths[7], &tmp))
        cfg->baseDef = tmp;
    if (values[8] && vm_mock_mysql_parse_u32(values[8], lengths[8], &tmp))
        cfg->baseMp = tmp;
    if (values[9] && vm_mock_mysql_parse_u32(values[9], lengths[9], &tmp))
        cfg->hpPerFloor = tmp;
    if (values[10] && vm_mock_mysql_parse_u32(values[10], lengths[10], &tmp))
        cfg->atkPerFloor = tmp;
    if (values[11] && vm_mock_mysql_parse_u32(values[11], lengths[11], &tmp))
        cfg->defPerFloor = tmp;
    if (values[12] && vm_mock_mysql_parse_u32(values[12], lengths[12], &tmp))
        cfg->damageScalePerFloor = (u16)tmp;
    if (values[13] && vm_mock_mysql_parse_u32(values[13], lengths[13], &tmp))
        cfg->castSkill = (u8)tmp;
    return true;
}

static bool vm_mock_ladder_reward_row(void *contextValue, unsigned int columnCount,
                                      const char *const *values, const size_t *lengths)
{
    u32 tmp = 0;

    (void)contextValue;
    (void)columnCount;
    if (values == NULL || lengths == NULL ||
        g_vm_mock_ladder_reward_count >= VM_MOCK_LADDER_REWARD_MAX)
        return false;
    {
        vm_mock_ladder_reward *rw =
            &g_vm_mock_ladder_rewards[g_vm_mock_ladder_reward_count++];
        memset(rw, 0, sizeof(*rw));
        if (values[0] && vm_mock_mysql_parse_u32(values[0], lengths[0], &tmp))
            rw->floorMilestone = (u16)tmp;
        if (values[1] && vm_mock_mysql_parse_u32(values[1], lengths[1], &tmp))
            rw->rewardExp = tmp;
        if (values[2] && vm_mock_mysql_parse_u32(values[2], lengths[2], &tmp))
            rw->rewardMoney = tmp;
        if (values[3] && vm_mock_mysql_parse_u32(values[3], lengths[3], &tmp))
            rw->rewardItemId = tmp;
        if (values[4] && vm_mock_mysql_parse_u32(values[4], lengths[4], &tmp))
            rw->rewardItemCount = tmp;
    }
    return true;
}

static void vm_mock_ladder_load_config(bool force)
{
    if (g_vm_mock_ladder_config_loaded && !force)
        return;
    (void)vm_mock_ladder_ensure_tables();
    memset(&g_vm_mock_ladder_config, 0, sizeof(g_vm_mock_ladder_config));
    g_vm_mock_ladder_config.enabled = 1;
    g_vm_mock_ladder_config.dailyLimit = 1;
    g_vm_mock_ladder_config.enemyIdTianji = 920001u;
    g_vm_mock_ladder_config.enemyIdHuanjian = 920002u;
    g_vm_mock_ladder_config.enemyIdGuidao = 920003u;
    g_vm_mock_ladder_config.baseHp = 800;
    g_vm_mock_ladder_config.baseAtk = 120;
    g_vm_mock_ladder_config.baseDef = 80;
    g_vm_mock_ladder_config.baseMp = 200;
    g_vm_mock_ladder_config.hpPerFloor = 150;
    g_vm_mock_ladder_config.atkPerFloor = 25;
    g_vm_mock_ladder_config.defPerFloor = 15;
    g_vm_mock_ladder_config.damageScalePerFloor = 5;
    g_vm_mock_ladder_config.castSkill = 1;
    g_vm_mock_ladder_reward_count = 0;
    /*
     * Do not mark loaded when MySQL is down / not ready yet — an early admin
     * page hit or WT 44/1 before connect used to cache defaults for the whole
     * process lifetime after restart.
     */
    if (vm_mysql_query(
            "SELECT enabled,daily_limit,enemy_id_tianji,enemy_id_huanjian,"
            "enemy_id_guidao,base_hp,base_atk,base_def,base_mp,hp_per_floor,"
            "atk_per_floor,def_per_floor,damage_scale_per_floor,cast_skill "
            "FROM server_ladder_config WHERE id=1 LIMIT 1",
            vm_mock_ladder_config_row, &g_vm_mock_ladder_config))
    {
        (void)vm_mysql_query(
            "SELECT floor_milestone,reward_exp,reward_money,reward_item_id,"
            "reward_item_count FROM server_ladder_rewards "
            "ORDER BY floor_milestone",
            vm_mock_ladder_reward_row, NULL);
        g_vm_mock_ladder_config_loaded = true;
    }
}

const vm_mock_ladder_config *vm_net_mock_ladder_config_get(void)
{
    vm_mock_ladder_load_config(false);
    return &g_vm_mock_ladder_config;
}

bool vm_mock_ladder_npc_offers(const char *npcName)
{
    vm_mock_ladder_load_config(false);
    if (!g_vm_mock_ladder_config.enabled || npcName == NULL || npcName[0] == 0)
        return false;
    /* 天梯使者 */
    return strstr(npcName, "\xcc\xec\xcc\xdd\xca\xb9\xd5\xdf") != NULL;
}

u32 vm_net_mock_ladder_reward_admin_count(void)
{
    vm_mock_ladder_load_config(false);
    return g_vm_mock_ladder_reward_count;
}

const vm_mock_ladder_reward *vm_net_mock_ladder_reward_admin_row(u32 index)
{
    vm_mock_ladder_load_config(false);
    if (index >= g_vm_mock_ladder_reward_count)
        return NULL;
    return &g_vm_mock_ladder_rewards[index];
}

static u32 vm_mock_ladder_day_ymd(void)
{
    time_t now = time(NULL);
    struct tm *tmNow = localtime(&now);
    if (tmNow == NULL)
        return 0;
    return (u32)((tmNow->tm_year + 1900) * 10000 +
                 (tmNow->tm_mon + 1) * 100 + tmNow->tm_mday);
}

static bool vm_mock_ladder_account_hex(char *hexOut, size_t hexCap)
{
    const char *accountId = g_vm_mock_service_active_account_id;

    if (accountId == NULL || accountId[0] == 0 || hexOut == NULL || hexCap == 0)
        return false;
    return vm_mysql_hex_encode(accountId, strlen(accountId), hexOut, hexCap) != 0;
}

static bool vm_mock_ladder_u16_row(void *contextValue, unsigned int columnCount,
                                   const char *const *values, const size_t *lengths)
{
    u16 *out = (u16 *)contextValue;
    u32 tmp = 0;

    (void)columnCount;
    if (out == NULL || values == NULL || lengths == NULL || values[0] == NULL)
        return false;
    if (!vm_mock_mysql_parse_u32(values[0], lengths[0], &tmp))
        return false;
    *out = (u16)tmp;
    return true;
}

static u16 vm_mock_ladder_role_best_floor(vm_net_mock_role_state *role)
{
    char accountHex[160];
    char query[320];
    u32 dayYmd = vm_mock_ladder_day_ymd();
    u16 best = 0;

    if (role == NULL || dayYmd == 0 ||
        !vm_mock_ladder_account_hex(accountHex, sizeof(accountHex)))
        return 0;
    snprintf(query, sizeof(query),
             "SELECT best_floor FROM server_ladder_daily "
             "WHERE account_id=UNHEX('%s') AND role_id=%u AND day_ymd=%u LIMIT 1",
             accountHex, role->roleId, dayYmd);
    (void)vm_mysql_query(query, vm_mock_ladder_u16_row, &best);
    return best;
}

static u16 vm_mock_ladder_role_daily_used(vm_net_mock_role_state *role)
{
    char accountHex[160];
    char query[320];
    u32 dayYmd = vm_mock_ladder_day_ymd();
    u16 used = 0;

    if (role == NULL || dayYmd == 0 ||
        !vm_mock_ladder_account_hex(accountHex, sizeof(accountHex)))
        return 0;
    snprintf(query, sizeof(query),
             "SELECT attempt_count FROM server_ladder_daily "
             "WHERE account_id=UNHEX('%s') AND role_id=%u AND day_ymd=%u LIMIT 1",
             accountHex, role->roleId, dayYmd);
    (void)vm_mysql_query(query, vm_mock_ladder_u16_row, &used);
    return used;
}

static bool vm_mock_ladder_try_consume_daily(vm_net_mock_role_state *role)
{
    char accountHex[160];
    char query[512];
    u32 dayYmd = vm_mock_ladder_day_ymd();
    u16 used = vm_mock_ladder_role_daily_used(role);
    u16 limit = g_vm_mock_ladder_config.dailyLimit;

    if (role == NULL || dayYmd == 0 || limit == 0)
        return false;
    if (used >= limit)
        return false;
    if (!vm_mock_ladder_account_hex(accountHex, sizeof(accountHex)))
        return false;
    snprintf(query, sizeof(query),
             "INSERT INTO server_ladder_daily "
             "(account_id,role_id,day_ymd,attempt_count) VALUES "
             "(UNHEX('%s'),%u,%u,1) "
             "ON DUPLICATE KEY UPDATE attempt_count=attempt_count+1",
             accountHex, role->roleId, dayYmd);
    return vm_mysql_exec(query);
}

static void vm_mock_ladder_compute_floor_stats(u16 floor, u32 *hpOut, u32 *mpOut)
{
    u32 f = floor > 0 ? (u32)floor : 1u;
    u32 hp = g_vm_mock_ladder_config.baseHp +
             g_vm_mock_ladder_config.hpPerFloor * (f - 1u);
    u32 mp = g_vm_mock_ladder_config.baseMp;

    if (hpOut)
        *hpOut = hp;
    if (mpOut)
        *mpOut = mp;
}

static u32 vm_net_mock_build_ladder_battle_start_info_blob(
    u8 *out, u32 outCap, u32 roleId, u32 roleHp, u32 roleMaxHp,
    u32 roleMp, u32 roleMaxMp, const vm_mock_ladder_battle_slot *slot)
{
    u32 pos = 0;
    static const char *jobNames[3] = {
        "\xcc\xec\xbb\xfa", /* 天机 */
        "\xbb\xc3\xbc\xaf", /* 幻剑 */
        "\xb9\xed\xb5\xc0"  /* 鬼道 */
    };
    u8 sexGroup = 1;
    u8 jobIndex = 0;

    if (slot == NULL)
        return 0;
    if (!vm_net_mock_seq_put_u8(out, outCap, &pos, VM_MOCK_LADDER_JOB_COUNT))
        return 0;
    for (u8 i = 0; i < VM_MOCK_LADDER_JOB_COUNT; ++i)
    {
        u32 enemyId = slot->enemyIds[i];
        u32 hp = slot->hp[i];
        u32 mp = slot->mp[i];
        jobIndex = i;
        if (!vm_net_mock_seq_put_u32(out, outCap, &pos, enemyId) ||
            !vm_net_mock_seq_put_u32(out, outCap, &pos, hp) ||
            !vm_net_mock_seq_put_u32(out, outCap, &pos, hp) ||
            !vm_net_mock_seq_put_u32(out, outCap, &pos, mp) ||
            !vm_net_mock_seq_put_u32(out, outCap, &pos, mp) ||
            !vm_net_mock_seq_put_string(out, outCap, &pos, jobNames[i]) ||
            !vm_net_mock_seq_put_u8(out, outCap, &pos, sexGroup) ||
            !vm_net_mock_seq_put_u8(out, outCap, &pos, jobIndex))
            return 0;
    }
    if (!vm_net_mock_seq_put_u8(out, outCap, &pos, 1) ||
        !vm_net_mock_seq_put_u32(out, outCap, &pos, roleId) ||
        !vm_net_mock_seq_put_u32(out, outCap, &pos, roleHp) ||
        !vm_net_mock_seq_put_u32(out, outCap, &pos, roleMaxHp) ||
        !vm_net_mock_seq_put_u32(out, outCap, &pos, roleMp) ||
        !vm_net_mock_seq_put_u32(out, outCap, &pos, roleMaxMp))
        return 0;
    return pos;
}

static u32 vm_net_mock_build_ladder_battle_response(
    u8 *out, u32 outCap, const vm_mock_ladder_battle_slot *slot)
{
    vm_net_mock_role_state *role = vm_net_mock_active_role();
    u32 roleHpDefault = VM_NET_MOCK_ROLE_DEFAULT_HP;
    u32 roleMaxHpDefault = VM_NET_MOCK_ROLE_DEFAULT_HP;
    u32 roleMpDefault = VM_NET_MOCK_ROLE_DEFAULT_MP;
    u32 roleMaxMpDefault = VM_NET_MOCK_ROLE_DEFAULT_MP;
    u32 roleId = role ? role->roleId : VM_NET_MOCK_ROLE_DEFAULT_ID;
    u32 roleHp = 0;
    u32 roleMaxHp = 0;
    u32 roleMp = 0;
    u32 roleMaxMp = 0;
    u8 battleInfo[512];
    u32 battleInfoLen = 0;
    u32 pos = 5;
    u32 objectStart = 0;

    if (slot == NULL || !slot->armed)
        return 0;
    vm_net_mock_role_default_vitals(role, &roleHpDefault, &roleMaxHpDefault,
                                    &roleMpDefault, &roleMaxMpDefault);
    roleHp = role ? role->hp : roleHpDefault;
    roleMaxHp = role ? role->hpMax : roleMaxHpDefault;
    roleMp = role ? role->mp : roleMpDefault;
    roleMaxMp = role ? role->mpMax : roleMaxMpDefault;
    if (roleMaxHp < roleHpDefault)
        roleMaxHp = roleHpDefault;
    if (roleMaxMp < roleMpDefault)
        roleMaxMp = roleMpDefault;
    if (roleHp > roleMaxHp)
        roleHp = roleMaxHp;
    if (roleMp > roleMaxMp)
        roleMp = roleMaxMp;
    battleInfoLen = vm_net_mock_build_ladder_battle_start_info_blob(
        battleInfo, sizeof(battleInfo), roleId, roleHp, roleMaxHp, roleMp,
        roleMaxMp, slot);
    if (battleInfoLen == 0)
        return 0;
    if (!vm_net_mock_begin_wt_object(out, outCap, &pos, 1, 4, 10,
                                     &objectStart) ||
        !vm_net_mock_put_object_u8(out, outCap, &pos, "side", 1) ||
        !vm_net_mock_put_object_raw(out, outCap, &pos, "battleinfo",
                                    battleInfo, (u16)battleInfoLen))
        return 0;
    vm_net_mock_finish_wt_object(out, objectStart, pos);
    vm_net_mock_finish_wt_packet(out, pos, 1);
    vm_net_mock_battle_arm_subtype10_multi_pve_start(
        roleId, roleHp, roleMaxHp, roleMp, roleMaxMp, slot->enemyIds[0],
        VM_MOCK_LADDER_JOB_COUNT, slot->hp, slot->enemyIds);
    {
        static const u8 ladderJobs[3] = {0, 1, 2}; /* 天机/幻剑/鬼道 */
        vm_net_mock_battle_set_enemy_job_slots(ladderJobs, VM_MOCK_LADDER_JOB_COUNT);
    }
    printf("[info][mock-service] ladder_battle_start role=%u floor=%u "
           "enemy=%u/%u/%u resp=%u\n",
           roleId, (u32)slot->floor, slot->enemyIds[0], slot->enemyIds[1],
           slot->enemyIds[2], pos);
    return pos;
}

static bool vm_net_mock_is_ladder44_request(const u8 *request, u32 requestLen,
                                            u8 *subtypeOut)
{
    u32 offset = 4;
    vm_net_mock_request_object object;

    if (subtypeOut)
        *subtypeOut = 0;
    if (request == NULL || requestLen < 9 || request[0] != 'W' ||
        request[1] != 'T')
        return false;
    if (!vm_net_mock_next_request_object(request, requestLen, &offset, &object))
        return false;
    if (object.major != 1 || object.kind != VM_MOCK_LADDER_WT_KIND)
        return false;
    if (subtypeOut)
        *subtypeOut = object.subtype;
    if (vm_net_mock_next_request_object(request, requestLen, &offset, &object))
        return false;
    return offset == requestLen;
}

static u32 vm_net_mock_build_ladder_status_response(u8 *out, u32 outCap)
{
    vm_net_mock_role_state *role = vm_net_mock_active_role();
    u16 best = vm_mock_ladder_role_best_floor(role);
    u16 used = vm_mock_ladder_role_daily_used(role);

    vm_mock_ladder_load_config(false);
    u16 nextFloor = (u16)(best + 1u);
    u32 pos = 5;
    u32 objectStart = 0;
    u8 result = 1;

    if (!g_vm_mock_ladder_config.enabled)
        result = 0;
    if (!vm_net_mock_begin_wt_object(out, outCap, &pos, 1, VM_MOCK_LADDER_WT_KIND,
                                     VM_MOCK_LADDER_WT_STATUS, &objectStart) ||
        !vm_net_mock_put_object_u8(out, outCap, &pos, "result", result) ||
        !vm_net_mock_put_object_u8(out, outCap, &pos, "enabled",
                                   g_vm_mock_ladder_config.enabled) ||
        !vm_net_mock_put_object_u32(out, outCap, &pos, "dailyLimit",
                                    g_vm_mock_ladder_config.dailyLimit) ||
        !vm_net_mock_put_object_u32(out, outCap, &pos, "dailyUsed", used) ||
        !vm_net_mock_put_object_u32(out, outCap, &pos, "bestFloor", best) ||
        !vm_net_mock_put_object_u32(out, outCap, &pos, "nextFloor", nextFloor))
        return 0;
    vm_net_mock_finish_wt_object(out, objectStart, pos);
    vm_net_mock_finish_wt_packet(out, pos, 1);
    return pos;
}

static u32 vm_net_mock_build_ladder_start_response(u8 *out, u32 outCap)
{
    vm_net_mock_role_state *role = vm_net_mock_active_role();
    u16 best = 0;
    u16 floor = 1;
    u32 hp = 0;
    u32 mp = 0;
    u32 pos = 5;
    u32 objectStart = 0;
    u8 result = 0;
    vm_mock_ladder_battle_slot *slot = NULL;
    static const char tipDisabledGbk[] =
        "\xcc\xec\xcc\xdd\xcc\xf4\xd5\xbd\xce\xb4\xbf\xaa\xc6\xf4"; /* 天梯挑战未开启 */
    static const char tipDailyGbk[] =
        "\xbd\xf1\xc8\xd5\xcc\xec\xcc\xdd\xb4\xce\xca\xfd\xd2\xd1\xd3\xc3\xcd\xea"; /* 今日天梯次数已用完 */
    static const char tipBusyGbk[] =
        "\xcc\xec\xcc\xdd\xc3\xbb\xd3\xd0\xbf\xd5\xcf\xd0"; /* 天梯没有空闲 */
    const char *tip = tipDisabledGbk;

    vm_mock_ladder_load_config(false);
    if (!g_vm_mock_ladder_config.enabled)
    {
        tip = tipDisabledGbk;
    }
    else if (role == NULL)
    {
        tip = tipDisabledGbk;
    }
    else if (vm_mock_ladder_role_daily_used(role) >=
             g_vm_mock_ladder_config.dailyLimit)
    {
        tip = tipDailyGbk;
    }
    else if ((slot = vm_mock_ladder_battle_alloc(role->roleId)) == NULL)
    {
        tip = tipBusyGbk;
    }
    else if (!vm_mock_ladder_try_consume_daily(role))
    {
        tip = tipDailyGbk;
    }
    else
    {
        best = vm_mock_ladder_role_best_floor(role);
        floor = (u16)(best + 1u);
        slot->floor = floor;
        slot->enemyIds[0] = g_vm_mock_ladder_config.enemyIdTianji;
        slot->enemyIds[1] = g_vm_mock_ladder_config.enemyIdHuanjian;
        slot->enemyIds[2] = g_vm_mock_ladder_config.enemyIdGuidao;
        vm_mock_ladder_compute_floor_stats(floor, &hp, &mp);
        for (u8 i = 0; i < VM_MOCK_LADDER_JOB_COUNT; ++i)
        {
            slot->hp[i] = hp;
            slot->mp[i] = mp;
        }
        slot->armed = 1;
        return vm_net_mock_build_ladder_battle_response(out, outCap, slot);
    }
    if (!vm_net_mock_begin_wt_object(out, outCap, &pos, 1, VM_MOCK_LADDER_WT_KIND,
                                     VM_MOCK_LADDER_WT_START, &objectStart) ||
        !vm_net_mock_put_object_u8(out, outCap, &pos, "result", result))
        return 0;
    if (tip != NULL &&
        !vm_net_mock_put_object_string(out, outCap, &pos, "tip", tip))
        return 0;
    vm_net_mock_finish_wt_object(out, objectStart, pos);
    vm_net_mock_finish_wt_packet(out, pos, 1);
    return pos;
}

static u32 vm_net_mock_build_ladder_protocol_response(const u8 *request,
                                                      u32 requestLen, u8 *out,
                                                      u32 outCap)
{
    u8 subtype = 0;

    if (!vm_net_mock_is_ladder44_request(request, requestLen, &subtype))
        return 0;
    if (subtype == VM_MOCK_LADDER_WT_STATUS)
        return vm_net_mock_build_ladder_status_response(out, outCap);
    if (subtype == VM_MOCK_LADDER_WT_START)
        return vm_net_mock_build_ladder_start_response(out, outCap);
    return 0;
}

bool vm_mock_ladder_battle_is_active(void)
{
    return vm_mock_ladder_battle_active_slot() != NULL;
}

u16 vm_mock_ladder_battle_floor(void)
{
    vm_mock_ladder_battle_slot *slot = vm_mock_ladder_battle_active_slot();

    return slot != NULL ? slot->floor : 1;
}

u32 vm_mock_ladder_damage_scale_percent(void)
{
    vm_mock_ladder_battle_slot *slot = vm_mock_ladder_battle_active_slot();
    u32 f = 1u;
    u32 scale = 0;

    if (slot != NULL && slot->floor > 0)
        f = (u32)slot->floor;
    scale = g_vm_mock_ladder_config.damageScalePerFloor * (f - 1u);
    if (scale > 500u)
        scale = 500u;
    return 100u + scale;
}

static bool vm_mock_ladder_persist_best_floor(vm_net_mock_role_state *role,
                                              u16 floor)
{
    char accountHex[160];
    char query[640];
    u32 dayYmd = vm_mock_ladder_day_ymd();
    bool okDaily = false;
    bool okProgress = false;

    if (role == NULL || floor == 0 || dayYmd == 0 ||
        !vm_mock_ladder_account_hex(accountHex, sizeof(accountHex)))
        return false;
    /*
     * UPSERT: UPDATE-only used to silently no-op if the daily row vanished
     * (day rollover mid-fight / consume failed to insert). Progress table is
     * lifetime mirror for ops; climb authority for nextFloor stays daily.
     */
    snprintf(query, sizeof(query),
             "INSERT INTO server_ladder_daily "
             "(account_id,role_id,day_ymd,attempt_count,best_floor) VALUES "
             "(UNHEX('%s'),%u,%u,0,%u) "
             "ON DUPLICATE KEY UPDATE "
             "best_floor=GREATEST(best_floor,VALUES(best_floor))",
             accountHex, role->roleId, dayYmd, (u32)floor);
    okDaily = vm_mysql_exec(query);
    snprintf(query, sizeof(query),
             "INSERT INTO server_ladder_progress "
             "(account_id,role_id,best_floor) VALUES "
             "(UNHEX('%s'),%u,%u) "
             "ON DUPLICATE KEY UPDATE "
             "best_floor=GREATEST(best_floor,VALUES(best_floor))",
             accountHex, role->roleId, (u32)floor);
    okProgress = vm_mysql_exec(query);
    if (!okDaily)
        printf("[warn][mock-service] ladder_best_floor_daily_fail role=%u "
               "floor=%u\n",
               role->roleId, (u32)floor);
    return okDaily || okProgress;
}

static const char *vm_mock_ladder_player_name_gbk(vm_net_mock_role_state *role)
{
    vm_mock_service_client_session *session =
        vm_mock_service_get_active_client_session();

    if (session != NULL && session->onlineRoleName[0] != 0)
        return session->onlineRoleName;
    if (role != NULL && role->name[0] != 0)
        return role->name;
    return NULL;
}

static bool vm_mock_ladder_u32_row(void *contextValue, unsigned int columnCount,
                                   const char *const *values, const size_t *lengths)
{
    u32 *out = (u32 *)contextValue;
    u32 tmp = 0;

    (void)columnCount;
    if (out == NULL || values == NULL || lengths == NULL || values[0] == NULL)
        return false;
    if (!vm_mock_mysql_parse_u32(values[0], lengths[0], &tmp))
        return false;
    *out = tmp;
    return true;
}

/* INSERT IGNORE first claim of (day, 10x floor); true only for the winner. */
static bool vm_mock_ladder_claim_daily_ten_lead(u16 floor, u32 roleId)
{
    char query[256];
    u32 dayYmd = vm_mock_ladder_day_ymd();
    u32 affected = 0;

    if (floor == 0 || (floor % 10u) != 0 || roleId == 0 || dayYmd == 0)
        return false;
    if (!vm_mock_ladder_ensure_tables())
        return false;
    snprintf(query, sizeof(query),
             "INSERT IGNORE INTO server_ladder_daily_lead "
             "(day_ymd,floor_n,role_id) VALUES (%u,%u,%u)",
             dayYmd, (u32)floor, roleId);
    if (!vm_mysql_exec(query))
        return false;
    if (!vm_mysql_query("SELECT ROW_COUNT()", vm_mock_ladder_u32_row, &affected))
        return false;
    return affected == 1u;
}

static void vm_mock_ladder_announce_daily_ten_lead(u16 floor,
                                                   const char *playerName)
{
    static const char systemNameGbk[] = "\xCF\xB5\xCD\xB3"; /* 系统 */
    char message[82];
    int wrote;
    u32 recipients;

    if (playerName == NULL || playerName[0] == 0 || floor == 0)
        return;
    /* 玩家{名}领先通过{N}层天梯 — ≤81 GBK wire. */
    memset(message, 0, sizeof(message));
    wrote = snprintf(
        message, sizeof(message),
        "\xCD\xE6\xBC\xD2%s\xC1\xEC\xCF\xC8\xCD\xA8\xB9\xFD%u"
        "\xB2\xE3\xCC\xEC\xCC\xDD",
        playerName, (u32)floor);
    if (wrote <= 0 || (size_t)wrote >= sizeof(message))
    {
        memset(message, 0, sizeof(message));
        wrote = snprintf(
            message, sizeof(message),
            "\xCD\xE6\xBC\xD2\xC1\xEC\xCF\xC8\xCD\xA8\xB9\xFD%u"
            "\xB2\xE3\xCC\xEC\xCC\xDD",
            (u32)floor);
    }
    if (wrote <= 0 || (size_t)wrote >= sizeof(message))
        return;
    recipients = vm_mock_service_broadcast_world_chat_live(systemNameGbk, message);
    printf("[info][mock-service] ladder_daily_lead floor=%u player=%s "
           "recipients=%u tip=%s\n",
           (u32)floor, playerName, recipients, message);
}

static bool vm_mock_ladder_tip_append(char *out, u32 outCap, u32 *used,
                                      const char *frag)
{
    size_t n;

    if (out == NULL || used == NULL || frag == NULL || outCap == 0)
        return false;
    n = strlen(frag);
    if (n == 0)
        return true;
    if (*used + n + 1u > outCap)
        return false;
    memcpy(out + *used, frag, n);
    *used += (u32)n;
    out[*used] = 0;
    return true;
}

/* Personal system tip: 恭喜玩家获得经验N、金钱N、物品xN */
static void vm_mock_ladder_notify_milestone_tip(u32 rewardExp, u32 rewardMoney,
                                                u32 rewardItemId,
                                                u32 rewardItemCount)
{
    static const char prefixGbk[] =
        "\xB9\xA7\xCF\xB2\xCD\xE6\xBC\xD2\xBB\xF1\xB5\xC3"; /* 恭喜玩家获得 */
    static const char sepGbk[] = "\xA1\xA2"; /* 、 */
    vm_mock_service_client_session *session =
        vm_mock_service_get_active_client_session();
    const vm_net_mock_shop_catalog_item *shop = NULL;
    char tip[82];
    char part[64];
    u32 used = 0;
    u32 parts = 0;

    if (session == NULL)
        return;
    if (rewardExp == 0 && rewardMoney == 0 &&
        (rewardItemId == 0 || rewardItemCount == 0))
        return;
    tip[0] = 0;
    if (!vm_mock_ladder_tip_append(tip, sizeof(tip), &used, prefixGbk))
        return;
    if (rewardExp)
    {
        snprintf(part, sizeof(part), "\xBE\xAD\xD1\xE9%u", rewardExp);
        if (vm_mock_ladder_tip_append(tip, sizeof(tip), &used, part))
            ++parts;
    }
    if (rewardMoney)
    {
        if (parts && !vm_mock_ladder_tip_append(tip, sizeof(tip), &used, sepGbk))
            goto enqueue;
        snprintf(part, sizeof(part), "\xBD\xF0\xC7\xAE%u", rewardMoney);
        if (vm_mock_ladder_tip_append(tip, sizeof(tip), &used, part))
            ++parts;
    }
    if (rewardItemId && rewardItemCount)
    {
        if (parts && !vm_mock_ladder_tip_append(tip, sizeof(tip), &used, sepGbk))
            goto enqueue;
        shop = vm_net_mock_find_shop_catalog_item(rewardItemId);
        if (shop != NULL && shop->name[0] != 0)
            snprintf(part, sizeof(part), "%sx%u", shop->name, rewardItemCount);
        else
            snprintf(part, sizeof(part), "#%ux%u", rewardItemId,
                     rewardItemCount);
        if (!vm_mock_ladder_tip_append(tip, sizeof(tip), &used, part))
        {
            snprintf(part, sizeof(part), "#%ux%u", rewardItemId,
                     rewardItemCount);
            if (vm_mock_ladder_tip_append(tip, sizeof(tip), &used, part))
                ++parts;
        }
        else
            ++parts;
    }
enqueue:
    if (parts == 0 || tip[0] == 0)
        return;
    if (vm_mock_service_session_enqueue_system_message(session, tip))
        printf("[info][mock-service] ladder_milestone_tip tip=%s\n", tip);
}

void vm_mock_ladder_on_battle_end(bool victory)
{
    vm_net_mock_role_state *role = vm_net_mock_active_role();
    vm_mock_ladder_battle_slot *slot = NULL;
    u16 floor = 0;
    u16 prevBest = 0;
    u32 i;
    u32 grantedExp = 0;
    u32 grantedMoney = 0;
    u32 grantedItemId = 0;
    u32 grantedItemCount = 0;
    const char *playerName = NULL;

    /*
     * Only the role that armed the ladder may clear/settle.  Other clients'
     * death/victory must not touch this climber's slot.
     */
    if (role == NULL)
        return;
    slot = vm_mock_ladder_battle_find(role->roleId);
    if (slot == NULL || !slot->armed)
        return;
    floor = slot->floor;
    slot->armed = 0;
    if (!victory)
    {
        printf("[info][mock-service] ladder_battle_end victory=0 role=%u "
               "floor=%u\n",
               role->roleId, (u32)floor);
        return;
    }
    prevBest = vm_mock_ladder_role_best_floor(role);
    (void)vm_mock_ladder_persist_best_floor(role, floor);
    playerName = vm_mock_ladder_player_name_gbk(role);
    if (floor > prevBest && (floor % 10u) == 0 &&
        vm_mock_ladder_claim_daily_ten_lead(floor, role->roleId))
        vm_mock_ladder_announce_daily_ten_lead(floor, playerName);
    for (i = 0; i < g_vm_mock_ladder_reward_count; ++i)
    {
        const vm_mock_ladder_reward *rw = &g_vm_mock_ladder_rewards[i];
        u16 itemSeq = 0;
        if (rw->floorMilestone == 0 || floor < rw->floorMilestone ||
            (floor % rw->floorMilestone) != 0)
            continue;
        if (rw->rewardExp)
        {
            (void)vm_net_mock_role_add_exp(role, rw->rewardExp);
            grantedExp = vm_net_mock_add_capped_u32(grantedExp, rw->rewardExp);
        }
        if (rw->rewardMoney)
        {
            role->money = vm_net_mock_add_capped_u32(role->money, rw->rewardMoney);
            grantedMoney =
                vm_net_mock_add_capped_u32(grantedMoney, rw->rewardMoney);
            vm_net_mock_role_mark_inventory_dirty("ladder-milestone-money");
        }
        if (rw->rewardItemId && rw->rewardItemCount)
        {
            (void)vm_net_mock_role_add_backpack_item_to_role(
                role, rw->rewardItemId, rw->rewardItemCount, 0, &itemSeq,
                "ladder-milestone-item", NULL, 0);
            /* Tip lists first item id; later different ids still granted. */
            if (grantedItemId == 0)
                grantedItemId = rw->rewardItemId;
            if (grantedItemId == rw->rewardItemId)
                grantedItemCount = vm_net_mock_add_capped_u32(
                    grantedItemCount, rw->rewardItemCount);
        }
    }
    if (grantedExp || grantedMoney ||
        (grantedItemId && grantedItemCount))
        vm_mock_ladder_notify_milestone_tip(grantedExp, grantedMoney,
                                            grantedItemId, grantedItemCount);
    vm_net_mock_role_db_save("ladder-victory");
    printf("[info][mock-service] ladder_victory role=%u floor=%u best=%u "
           "milestone_exp=%u money=%u item=%u x%u\n",
           role->roleId, (u32)floor,
           (u32)vm_mock_ladder_role_best_floor(role), grantedExp, grantedMoney,
           grantedItemId, grantedItemCount);
}

bool vm_mock_ladder_admin_save_config(
    u8 enabled, u16 dailyLimit, u32 enemyTianji, u32 enemyHuanjian,
    u32 enemyGuidao, u32 baseHp, u32 baseAtk, u32 baseDef, u32 baseMp,
    u32 hpPerFloor, u32 atkPerFloor, u32 defPerFloor, u16 damageScale,
    u8 castSkill)
{
    char query[1024];

    if (!vm_mock_ladder_ensure_tables())
        return false;
    snprintf(
        query, sizeof(query),
        "INSERT INTO server_ladder_config "
        "(id,enabled,daily_limit,enemy_id_tianji,enemy_id_huanjian,"
        "enemy_id_guidao,base_hp,base_atk,base_def,base_mp,hp_per_floor,"
        "atk_per_floor,def_per_floor,damage_scale_per_floor,cast_skill) "
        "VALUES (1,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u) "
        "ON DUPLICATE KEY UPDATE "
        "enabled=VALUES(enabled),daily_limit=VALUES(daily_limit),"
        "enemy_id_tianji=VALUES(enemy_id_tianji),"
        "enemy_id_huanjian=VALUES(enemy_id_huanjian),"
        "enemy_id_guidao=VALUES(enemy_id_guidao),base_hp=VALUES(base_hp),"
        "base_atk=VALUES(base_atk),base_def=VALUES(base_def),"
        "base_mp=VALUES(base_mp),hp_per_floor=VALUES(hp_per_floor),"
        "atk_per_floor=VALUES(atk_per_floor),def_per_floor=VALUES(def_per_floor),"
        "damage_scale_per_floor=VALUES(damage_scale_per_floor),"
        "cast_skill=VALUES(cast_skill)",
        (u32)enabled, (u32)dailyLimit, enemyTianji, enemyHuanjian, enemyGuidao,
        baseHp, baseAtk, baseDef, baseMp, hpPerFloor, atkPerFloor, defPerFloor,
        (u32)damageScale, (u32)castSkill);
    if (!vm_mysql_exec(query))
        return false;
    g_vm_mock_ladder_config_loaded = false;
    vm_mock_ladder_load_config(true);
    return true;
}

bool vm_mock_ladder_admin_save_reward(u16 floorMilestone, u32 rewardExp,
                                      u32 rewardMoney, u32 rewardItemId,
                                      u32 rewardItemCount)
{
    char query[512];

    if (floorMilestone == 0)
        return false;
    if (!vm_mock_ladder_ensure_tables())
        return false;
    snprintf(
        query, sizeof(query),
        "INSERT INTO server_ladder_rewards "
        "(floor_milestone,reward_exp,reward_money,reward_item_id,reward_item_count) "
        "VALUES (%u,%u,%u,%u,%u) "
        "ON DUPLICATE KEY UPDATE reward_exp=VALUES(reward_exp),"
        "reward_money=VALUES(reward_money),"
        "reward_item_id=VALUES(reward_item_id),"
        "reward_item_count=VALUES(reward_item_count)",
        (u32)floorMilestone, rewardExp, rewardMoney, rewardItemId,
        rewardItemCount);
    if (!vm_mysql_exec(query))
        return false;
    g_vm_mock_ladder_config_loaded = false;
    vm_mock_ladder_load_config(true);
    return true;
}

bool vm_mock_ladder_admin_delete_reward(u16 floorMilestone)
{
    char query[128];

    if (floorMilestone == 0)
        return false;
    snprintf(query, sizeof(query),
             "DELETE FROM server_ladder_rewards WHERE floor_milestone=%u",
             (u32)floorMilestone);
    if (!vm_mysql_exec(query))
        return false;
    g_vm_mock_ladder_config_loaded = false;
    vm_mock_ladder_load_config(true);
    return true;
}
