/* Server maintenance mode: admin toggle, world notice, delayed mass kick, login block. */

#include <time.h>

#define VM_MOCK_MAINTENANCE_KICK_DELAY_SEC 120u
#define VM_MOCK_MAINTENANCE_NOTICE_INTERVAL_SEC 30u

typedef struct vm_mock_maintenance_state
{
    u8 active;
    u8 kickDone;
    u32 kickAtUnix;
    u32 lastNoticeUnix;
} vm_mock_maintenance_state;

static vm_mock_maintenance_state g_vm_mock_maintenance;
static bool g_vm_mock_maintenance_loaded = false;

/* 服务器维护中 */
static const char g_vm_mock_maintenance_login_msg_gbk[] =
    "\xB7\xFE\xCE\xF1\xC6\xF7\xCE\xAC\xBB\xA4\xD6\xD0";

static u32 vm_mock_service_broadcast_world_chat_live(const char *sourceName,
                                                     const char *message);
static void vm_mock_service_account_flush_for_session(
    const vm_mock_service_client_session *session, const char *reason);
static void vm_mock_service_session_mark_offline(
    vm_mock_service_client_session *session, const char *reason);
static void vm_mock_service_session_unbind_account(
    vm_mock_service_client_session *session);
static vm_mock_service_client_session *g_vm_mock_service_client_sessions;

static bool vm_mock_maintenance_ensure_tables(void)
{
    static bool ready = false;

    if (ready)
        return true;
    if (!vm_mysql_exec(
            "CREATE TABLE IF NOT EXISTS server_maintenance_config ("
            "id TINYINT UNSIGNED NOT NULL DEFAULT 1,"
            "active TINYINT UNSIGNED NOT NULL DEFAULT 0,"
            "kick_done TINYINT UNSIGNED NOT NULL DEFAULT 0,"
            "kick_at_unix INT UNSIGNED NOT NULL DEFAULT 0,"
            "last_notice_unix INT UNSIGNED NOT NULL DEFAULT 0,"
            "updated_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP "
            "ON UPDATE CURRENT_TIMESTAMP,"
            "PRIMARY KEY (id)) ENGINE=InnoDB"))
        return false;
    (void)vm_mysql_exec(
        "ALTER TABLE server_maintenance_config ADD COLUMN "
        "last_notice_unix INT UNSIGNED NOT NULL DEFAULT 0");
    ready = true;
    return true;
}

static bool vm_mock_maintenance_config_row(void *contextValue,
                                          unsigned int columnCount,
                                          const char *const *values,
                                          const size_t *lengths)
{
    vm_mock_maintenance_state *cfg = (vm_mock_maintenance_state *)contextValue;
    u32 tmp = 0;

    (void)columnCount;
    if (cfg == NULL || values == NULL || lengths == NULL)
        return false;
    memset(cfg, 0, sizeof(*cfg));
    if (values[0] && vm_mock_mysql_parse_u32(values[0], lengths[0], &tmp))
        cfg->active = (u8)tmp;
    if (values[1] && vm_mock_mysql_parse_u32(values[1], lengths[1], &tmp))
        cfg->kickDone = (u8)tmp;
    if (values[2] && vm_mock_mysql_parse_u32(values[2], lengths[2], &tmp))
        cfg->kickAtUnix = tmp;
    if (values[3] && vm_mock_mysql_parse_u32(values[3], lengths[3], &tmp))
        cfg->lastNoticeUnix = tmp;
    return true;
}

static bool vm_mock_maintenance_save_config(void)
{
    char query[512];

    if (!vm_mock_maintenance_ensure_tables())
        return false;
    snprintf(
        query, sizeof(query),
        "INSERT INTO server_maintenance_config "
        "(id,active,kick_done,kick_at_unix,last_notice_unix) VALUES "
        "(1,%u,%u,%u,%u) "
        "ON DUPLICATE KEY UPDATE "
        "active=VALUES(active),kick_done=VALUES(kick_done),"
        "kick_at_unix=VALUES(kick_at_unix),"
        "last_notice_unix=VALUES(last_notice_unix)",
        (u32)g_vm_mock_maintenance.active,
        (u32)g_vm_mock_maintenance.kickDone,
        g_vm_mock_maintenance.kickAtUnix,
        g_vm_mock_maintenance.lastNoticeUnix);
    return vm_mysql_exec(query);
}

static void vm_mock_maintenance_load_config(void)
{
    vm_mock_maintenance_state loaded;

    memset(&loaded, 0, sizeof(loaded));
    if (!vm_mock_maintenance_ensure_tables())
        return;
    (void)vm_mysql_query(
        "SELECT active,kick_done,kick_at_unix,last_notice_unix "
        "FROM server_maintenance_config WHERE id=1 LIMIT 1",
        vm_mock_maintenance_config_row, &loaded);
    g_vm_mock_maintenance = loaded;
    g_vm_mock_maintenance_loaded = true;
}

static void vm_mock_maintenance_ensure_loaded(void)
{
    if (!g_vm_mock_maintenance_loaded)
        vm_mock_maintenance_load_config();
}

static u32 vm_mock_maintenance_count_online_sessions(void)
{
    vm_mock_service_client_session *session = g_vm_mock_service_client_sessions;
    u32 count = 0;

    while (session != NULL)
    {
        if (session->accountId[0] != 0)
            ++count;
        session = session->next;
    }
    return count;
}

static void vm_mock_maintenance_broadcast_countdown(u32 secondsLeft, u32 now)
{
    static const char systemNameGbk[] = "\xCF\xB5\xCD\xB3"; /* 系统 */
    char message[82];
    int wrote = 0;
    u32 recipients = 0;

    if (secondsLeft == 0)
        return;
    memset(message, 0, sizeof(message));
    /* 还剩%u秒维护，请尽快下线 */
    wrote = snprintf(
        message, sizeof(message),
        "\xBB\xB9\xD3\xD0%u\xC3\xEB\xCE\xAC\xBB\xA4\xA3\xAC\xC7\xEB\xBE\xA1\xBF\xEC"
        "\xCF\xC2\xCF\xDF",
        secondsLeft);
    if (wrote <= 0 || (size_t)wrote >= sizeof(message))
        return;
    recipients = vm_mock_service_broadcast_world_chat_live(systemNameGbk, message);
    g_vm_mock_maintenance.lastNoticeUnix = now;
    (void)vm_mock_maintenance_save_config();
    printf("[info][mock-service] maintenance_world_notice recipients=%u "
           "seconds_left=%u kick_at=%u\n",
           recipients, secondsLeft, g_vm_mock_maintenance.kickAtUnix);
}

static void vm_mock_maintenance_maybe_broadcast_notice(u32 now)
{
    u32 secondsLeft = 0;

    if (g_vm_mock_maintenance.kickDone || g_vm_mock_maintenance.kickAtUnix == 0 ||
        now >= g_vm_mock_maintenance.kickAtUnix)
        return;
    secondsLeft = g_vm_mock_maintenance.kickAtUnix - now;
    if (g_vm_mock_maintenance.lastNoticeUnix != 0 &&
        now < g_vm_mock_maintenance.lastNoticeUnix +
                 VM_MOCK_MAINTENANCE_NOTICE_INTERVAL_SEC)
        return;
    vm_mock_maintenance_broadcast_countdown(secondsLeft, now);
}

static void vm_mock_maintenance_kick_all_online(void)
{
    vm_mock_service_client_session *session = g_vm_mock_service_client_sessions;
    u32 kicked = 0;

    while (session != NULL)
    {
        vm_mock_service_client_session *next = session->next;

        if (session->accountId[0] != 0 && !session->maintenanceKickPending)
        {
            vm_mock_service_account_flush_for_session(session, "maintenance-kick");
            session->maintenanceKickPending = true;
            vm_mock_service_session_mark_offline(session, "maintenance-kick");
            vm_mock_service_session_unbind_account(session);
            ++kicked;
        }
        session = next;
    }
    printf("[info][mock-service] maintenance_kick_armed count=%u kick_at=%u\n",
           kicked, g_vm_mock_maintenance.kickAtUnix);
}

static void vm_mock_service_maintenance_tick(void)
{
    u32 now = 0;

    vm_mock_maintenance_ensure_loaded();
    if (!g_vm_mock_maintenance.active)
        return;

    now = (u32)time(NULL);
    vm_mock_maintenance_maybe_broadcast_notice(now);
    if (!g_vm_mock_maintenance.kickDone &&
        g_vm_mock_maintenance.kickAtUnix != 0 &&
        now >= g_vm_mock_maintenance.kickAtUnix)
    {
        vm_mock_maintenance_kick_all_online();
        g_vm_mock_maintenance.kickDone = 1;
        (void)vm_mock_maintenance_save_config();
    }
}

bool vm_mock_service_maintenance_blocks_login(void)
{
    vm_mock_maintenance_ensure_loaded();
    return g_vm_mock_maintenance.active != 0;
}

const char *vm_mock_service_maintenance_login_message_gbk(void)
{
    return g_vm_mock_maintenance_login_msg_gbk;
}

void vm_mock_maintenance_admin_get(u8 *activeOut, u32 *lastNoticeUnixOut,
                                   u8 *kickDoneOut, u32 *kickAtUnixOut,
                                   u32 *onlineCountOut, u32 *secondsLeftOut)
{
    u32 now = 0;
    u32 left = 0;

    vm_mock_maintenance_ensure_loaded();
    if (activeOut)
        *activeOut = g_vm_mock_maintenance.active;
    if (lastNoticeUnixOut)
        *lastNoticeUnixOut = g_vm_mock_maintenance.lastNoticeUnix;
    if (kickDoneOut)
        *kickDoneOut = g_vm_mock_maintenance.kickDone;
    if (kickAtUnixOut)
        *kickAtUnixOut = g_vm_mock_maintenance.kickAtUnix;
    if (onlineCountOut)
        *onlineCountOut = vm_mock_maintenance_count_online_sessions();
    if (secondsLeftOut)
    {
        now = (u32)time(NULL);
        if (g_vm_mock_maintenance.active && !g_vm_mock_maintenance.kickDone &&
            g_vm_mock_maintenance.kickAtUnix > now)
            left = g_vm_mock_maintenance.kickAtUnix - now;
        *secondsLeftOut = left;
    }
}

bool vm_mock_maintenance_admin_start(char *msgOut, size_t msgCap)
{
    u32 now = 0;

    vm_mock_maintenance_ensure_loaded();
    if (!vm_mock_maintenance_ensure_tables())
    {
        if (msgOut && msgCap > 0)
            snprintf(msgOut, msgCap, "数据库表初始化失败");
        return false;
    }
    now = (u32)time(NULL);
    g_vm_mock_maintenance.active = 1;
    g_vm_mock_maintenance.kickDone = 0;
    g_vm_mock_maintenance.kickAtUnix = now + VM_MOCK_MAINTENANCE_KICK_DELAY_SEC;
    g_vm_mock_maintenance.lastNoticeUnix = 0;
    if (!vm_mock_maintenance_save_config())
    {
        if (msgOut && msgCap > 0)
            snprintf(msgOut, msgCap, "保存维护配置失败");
        return false;
    }
    vm_mock_maintenance_broadcast_countdown(VM_MOCK_MAINTENANCE_KICK_DELAY_SEC, now);
    if (msgOut && msgCap > 0)
        snprintf(msgOut, msgCap,
                 "已开启维护：倒计时期间每30秒全服公告，2分钟后踢人并禁止登录");
    printf("[info][mock-service] maintenance_start kick_at=%u online=%u\n",
           g_vm_mock_maintenance.kickAtUnix,
           vm_mock_maintenance_count_online_sessions());
    return true;
}

bool vm_mock_maintenance_admin_stop(char *msgOut, size_t msgCap)
{
    vm_mock_maintenance_ensure_loaded();
    g_vm_mock_maintenance.active = 0;
    g_vm_mock_maintenance.kickDone = 0;
    g_vm_mock_maintenance.kickAtUnix = 0;
    g_vm_mock_maintenance.lastNoticeUnix = 0;
    if (!vm_mock_maintenance_save_config())
    {
        if (msgOut && msgCap > 0)
            snprintf(msgOut, msgCap, "关闭维护失败");
        return false;
    }
    if (msgOut && msgCap > 0)
        snprintf(msgOut, msgCap, "已关闭维护，玩家可以登录");
    printf("[info][mock-service] maintenance_stop\n");
    return true;
}
