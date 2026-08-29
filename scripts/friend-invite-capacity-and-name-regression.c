/*
 * Server-only regression for the nearby-player friend invitation contract.
 *
 * It does not open a listener, connect to MySQL, modify a client cache, or
 * touch a user account.  It supplies an in-process MySQL fixture reporting
 * 256 existing rows, then verifies that the real two-row accept transaction
 * remains possible without loading a global friend snapshot.  The second
 * assertion builds normal scene-poll notices and checks their C-string names.
 */

#include <stdio.h>
#include <string.h>

#define vm_mysql_query fixture_mysql_query
#define vm_mysql_exec fixture_mysql_exec
#define vm_mysql_last_error fixture_mysql_last_error
#define main cbe_server_program_main
#include "../src/server_main.c"
#undef main
#undef vm_mysql_last_error
#undef vm_mysql_exec
#undef vm_mysql_query

static u32 g_fixtureFriendCountQueries = 0;
static u32 g_fixturePairQueries = 0;
static u32 g_fixturePageQueries = 0;
static u32 g_fixtureTransactions = 0;
static u32 g_fixtureInserts = 0;
static u32 g_fixtureCommits = 0;

bool fixture_mysql_query(const char *sql, vm_mysql_row_callback callback,
                         void *context)
{
    static const char countValue[] = "256";
    const char *values[1] = { countValue };
    size_t lengths[1] = { sizeof(countValue) - 1u };

    if (sql == NULL)
        return false;
    if (strcmp(sql, "SELECT COUNT(*) FROM friendships") == 0)
    {
        ++g_fixtureFriendCountQueries;
        return callback == NULL || callback(context, 1, values, lengths);
    }
    if (strstr(sql, "ORDER BY target_account_id,target_role_id LIMIT 5 OFFSET 7") != NULL)
    {
        static const char *const row[] = {
            "invite.source", "10001", "invite.target", "10002",
            "546172676574", "1", "30", "1", "0"
        };
        size_t rowLengths[9];

        ++g_fixturePageQueries;
        for (u32 i = 0; i < 9; ++i)
            rowLengths[i] = strlen(row[i]);
        return callback == NULL || callback(context, 9, row, rowLengths);
    }
    if (strstr(sql, "FROM friendships WHERE") != NULL)
    {
        /* Both precise pair lookups report absent rows.  An empty result is
         * success: the following two INSERTs are the acceptance action. */
        ++g_fixturePairQueries;
        return true;
    }
    return false;
}

bool fixture_mysql_exec(const char *sql)
{
    if (sql == NULL)
        return false;
    if (strcmp(sql, "START TRANSACTION") == 0)
    {
        ++g_fixtureTransactions;
        return true;
    }
    if (strncmp(sql, "INSERT INTO friendships", 23) == 0)
    {
        ++g_fixtureInserts;
        return true;
    }
    if (strcmp(sql, "COMMIT") == 0)
    {
        ++g_fixtureCommits;
        return true;
    }
    return false;
}

const char *fixture_mysql_last_error(void)
{
    return "friend-regression-fixture";
}

static bool assert_friend_pair_is_on_demand(void)
{
    bool created = false;
    vm_mock_service_friend_record pageRecords[5];
    u32 pageRecordCount = 0;

    g_vm_mock_service_friend_db_loaded = false;
    g_vm_mock_service_friend_db_valid = false;
    if (!vm_mock_service_friend_db_add_pair(
            "invite.source", 10001u, "Source", 31u, 2u, 1u,
            "invite.target", 10002u, "Target", 30u, 1u, 0u,
            &created) ||
        !created || g_fixtureFriendCountQueries != 1u ||
        g_fixturePairQueries != 2u || g_fixtureTransactions != 1u ||
        g_fixtureInserts != 2u || g_fixtureCommits != 1u)
    {
        return false;
    }
    memset(pageRecords, 0, sizeof(pageRecords));
    return vm_mock_service_friend_record_query_page(
               10001u, "invite.source", 7u, 5u,
               pageRecords, 5u, &pageRecordCount) &&
           g_fixturePageQueries == 1u && pageRecordCount == 1u &&
           pageRecords[0].targetRoleId == 10002u &&
           strcmp(pageRecords[0].targetRoleName, "Target") == 0;
}

static bool assert_friend_name_notice(u8 noticeType, u8 expectedSubtype)
{
    static const char peerName[] = "Peer";
    vm_mock_service_client_session observer;
    vm_net_mock_response_object object;
    const u8 *wireName = NULL;
    u16 wireNameLen = 0;
    u8 noticeTypeOut = VM_MOCK_SERVICE_SOCIAL_NOTICE_NONE;
    u8 response[256];
    u32 pos = 5;
    u32 offset = 5;

    memset(&observer, 0, sizeof(observer));
    memset(&object, 0, sizeof(object));
    memset(response, 0, sizeof(response));
    observer.clientId = 0x10020003u;
    observer.socialNotices[0].type = noticeType;
    observer.socialNotices[0].result = 1;
    observer.socialNotices[0].sourceClientId = 0x10020004u;
    observer.socialNotices[0].sourceRoleId = 10004u;
    snprintf(observer.socialNotices[0].sourceName,
             sizeof(observer.socialNotices[0].sourceName), "%s", peerName);

    if (vm_net_mock_append_scene_sync_social_notice_object(
            response, sizeof(response), &pos, &observer, &noticeTypeOut) != 1 ||
        noticeTypeOut != noticeType)
    {
        return false;
    }
    vm_net_mock_finish_wt_packet(response, pos, 1);
    if (!vm_net_mock_next_response_object(response, pos, &offset, &object) ||
        offset != pos || object.major != 1 || object.kind != 10 ||
        object.subtype != expectedSubtype ||
        !vm_net_mock_get_object_blob_field(response, pos, "name",
                                           &wireName, &wireNameLen) ||
        wireNameLen != sizeof(peerName) ||
        memcmp(wireName, peerName, sizeof(peerName)) != 0 ||
        wireName[wireNameLen - 1] != '\0')
    {
        return false;
    }
    return true;
}

int main(void)
{
    if (!assert_friend_pair_is_on_demand())
    {
        fputs("friend invitation acceptance still depends on a global row cap\n",
              stderr);
        return 1;
    }
    if (!assert_friend_name_notice(VM_MOCK_SERVICE_SOCIAL_NOTICE_FRIEND_INVITE, 4u) ||
        !assert_friend_name_notice(VM_MOCK_SERVICE_SOCIAL_NOTICE_FRIEND_RESULT, 6u))
    {
        fputs("friend invitation/result notice name is not a NUL-terminated WT field\n",
              stderr);
        return 1;
    }
    puts("friend invitation regression passed: 256-row MySQL fixture writes pair on demand; WT10/4 and WT10/6 name C strings");
    return 0;
}
