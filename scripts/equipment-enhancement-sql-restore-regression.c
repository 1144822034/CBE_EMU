/*
 * Parser/policy regression for the administrative historical SQL restore.
 *
 * This fixture does not open MySQL, start listeners, or write any role data.
 * It includes the production parser and validates the restore-specific source
 * contract: both equipment tables, exact primary-key uniqueness, +16 range,
 * and four-stage affix encoding.
 */

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;

#define static
#define vm_mock_admin_ascii_ncasecmp fixture_ascii_ncasecmp
#define scheduler_get_tick_ms fixture_tick
#define vm_mock_admin_account_is_online fixture_account_online
#define vm_mysql_hex_encode fixture_mysql_hex_encode
#define vm_mysql_query fixture_mysql_query
#define vm_mysql_exec fixture_mysql_exec
#define vm_mock_admin_text fixture_admin_text
#define vm_mock_admin_text_init fixture_admin_text_init
#define vm_mock_admin_form_value fixture_form_value
#define vm_net_mock_parse_u32_strict fixture_parse_u32
#define vm_mock_admin_text_appendf fixture_text_appendf
#define vm_mock_admin_text_append_html fixture_text_append_html
#define vm_mock_admin_url_encode fixture_url_encode
#define vm_mock_admin_send_response fixture_send_response
#define vm_mock_service_socket int
#define VM_MOCK_ADMIN_ROOT_PATH "/admin/"

typedef struct { int unused; } fixture_admin_text;
static int fixture_ascii_ncasecmp(const char *a, const char *b, size_t n)
{
    for (size_t i = 0; i < n; ++i)
    {
        unsigned char left = (unsigned char)a[i];
        unsigned char right = (unsigned char)b[i];
        if (left >= 'A' && left <= 'Z') left = (unsigned char)(left + 32);
        if (right >= 'A' && right <= 'Z') right = (unsigned char)(right + 32);
        if (left != right) return (int)left - (int)right;
    }
    return 0;
}
static u32 fixture_tick(void) { return 1; }
static bool fixture_account_online(const char *account) { (void)account; return false; }
static size_t fixture_mysql_hex_encode(const void *data, size_t len, char *out, size_t cap)
{ (void)data; (void)len; (void)out; (void)cap; return 0; }
static bool fixture_mysql_query(const char *sql, void *cb, void *context)
{ (void)sql; (void)cb; (void)context; return false; }
static bool fixture_mysql_exec(const char *sql) { (void)sql; return false; }
static void fixture_admin_text_init(fixture_admin_text *t, char *out, size_t cap)
{ (void)t; (void)out; (void)cap; }
static bool fixture_form_value(const char *form, const char *key, char *out, size_t cap)
{ (void)form; (void)key; if (cap) out[0] = 0; return false; }
static bool fixture_parse_u32(const char *text, u32 *value)
{ (void)text; if (value) *value = 0; return false; }
static void fixture_text_appendf(fixture_admin_text *t, const char *fmt, ...)
{ (void)t; (void)fmt; }
static void fixture_text_append_html(fixture_admin_text *t, const char *text)
{ (void)t; (void)text; }
static void fixture_url_encode(const char *value, char *out, size_t cap)
{ (void)value; if (cap) out[0] = 0; }
static int fixture_send_response(int client, const char *status, const char *type,
                                 const char *headers, const char *body)
{ (void)client; (void)status; (void)type; (void)headers; (void)body; return 1; }

#include "../src/web_admin_equipment_restore.inc.c"

static bool parse_fixture(const char *sql, vm_mock_admin_enhance_restore_state *state)
{
    memset(state, 0, sizeof(*state));
    state->rows = calloc(VM_MOCK_ADMIN_ENHANCE_RESTORE_MAX_ROWS,
                         sizeof(*state->rows));
    return state->rows != NULL &&
           vm_mock_admin_enhance_restore_parse(sql, strlen(sql), state);
}

static int expect_valid_sources(void)
{
    /* Types 8,4,7,3 and values 2,20,3,12 use the production lane order. */
    const char *sql =
        "INSERT INTO `jh_online`.`account_role_equipment` "
        "(`account_id`,`role_id`,`slot_index`,`item_id`,`enhance_level`,"
        "`enhance_affix_types`,`enhance_affix_values`,`durability`,`durability_max`) VALUES "
        "('restore-a',10001,0,1001,16,100992003,1125912791875585,80,100);"
        "INSERT INTO account_role_backpack VALUES "
        "('restore-a',10001,5,1002,31,1,8,100992003,1125912791875585,70,100),"
        "('restore-b',10002,6,1003,32,1,0,0,0,60,100);";
    vm_mock_admin_enhance_restore_state state;
    bool ok = parse_fixture(sql, &state);
    if (!ok || state.count != 3 || state.equipmentCount != 1 ||
        state.backpackCount != 2 || state.rows[0].enhanceLevel != 16 ||
        state.rows[1].itemSeq != 31)
    {
        fputs("valid historical equipment/backpack INSERT was rejected\n", stderr);
        free(state.rows);
        return 1;
    }
    free(state.rows);
    return 0;
}

static int expect_rejected_sources(void)
{
    static const char *bad[] = {
        /* Database primary key duplicates even when item identity differs. */
        "INSERT INTO account_role_equipment (account_id,role_id,slot_index,item_id,enhance_level,enhance_affix_types,enhance_affix_values) VALUES ('dup',1,0,1001,4,8,2),('dup',1,0,1002,8,4,3);",
        /* Enhancement level must be within the native +16 contract. */
        "INSERT INTO account_role_equipment (account_id,role_id,slot_index,item_id,enhance_level,enhance_affix_types,enhance_affix_values) VALUES ('level',1,0,1001,17,8,2);",
        /* Type 1 is not one of the generated stage attributes. */
        "INSERT INTO account_role_equipment (account_id,role_id,slot_index,item_id,enhance_level,enhance_affix_types,enhance_affix_values) VALUES ('type',1,0,1001,4,1,2);",
        /* Non-zero type must have a non-zero value. */
        "INSERT INTO account_role_equipment (account_id,role_id,slot_index,item_id,enhance_level,enhance_affix_types,enhance_affix_values) VALUES ('zero',1,0,1001,4,8,0);",
        /* A future-stage plan cannot repeat an attribute type. */
        "INSERT INTO account_role_equipment (account_id,role_id,slot_index,item_id,enhance_level,enhance_affix_types,enhance_affix_values) VALUES ('repeat',1,0,1001,4,2056,131074);",
        /* Stage values use the same signed-positive ceiling as serialization. */
        "INSERT INTO account_role_equipment (account_id,role_id,slot_index,item_id,enhance_level,enhance_affix_types,enhance_affix_values) VALUES ('value',1,0,1001,4,8,32768);"
    };

    for (size_t i = 0; i < sizeof(bad) / sizeof(bad[0]); ++i)
    {
        vm_mock_admin_enhance_restore_state state;
        if (parse_fixture(bad[i], &state))
        {
            fprintf(stderr, "invalid restore source %u was accepted\n", (unsigned)i);
            free(state.rows);
            return 1;
        }
        free(state.rows);
    }
    return 0;
}

static int expect_historical_values_really_overwrite(void)
{
    vm_mock_admin_enhance_restore_row row;

    memset(&row, 0, sizeof(row));
    row.found = true;
    row.currentLevel = 16;
    row.currentAffixTypes = 100992003;
    row.currentAffixValues = 1125912791875585ULL;
    row.enhanceLevel = 8;
    row.enhanceAffixTypes = 8;
    row.enhanceAffixValues = 2;
    if (!vm_mock_admin_enhance_restore_row_changed(&row))
    {
        fputs("historical lower enhancement was incorrectly treated as a merge skip\n",
              stderr);
        return 1;
    }
    row.currentLevel = row.enhanceLevel;
    row.currentAffixTypes = row.enhanceAffixTypes;
    row.currentAffixValues = row.enhanceAffixValues;
    if (vm_mock_admin_enhance_restore_row_changed(&row))
    {
        fputs("identical historical enhancement was incorrectly marked changed\n",
              stderr);
        return 1;
    }
    return 0;
}

int main(void)
{
    if (expect_valid_sources() != 0 || expect_rejected_sources() != 0 ||
        expect_historical_values_really_overwrite() != 0)
        return 1;
    puts("equipment enhancement SQL restore regression passed: target INSERT rows parse, historical lower values remain overwrite candidates, and duplicate keys/overlevel/invalid affixes are rejected");
    return 0;
}
