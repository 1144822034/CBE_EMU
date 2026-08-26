#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define main cbe_server_program_main
#include "../src/server_main.c"
#undef main

static bool verify_scene_battle_draft_catalog_contract(void)
{
    static const char scene[] = "draft-catalog.sce";
    static const char name[] = "draft-monster";
    static const char monsterId[] = "60000";
    const char *values[] = {scene, monsterId, name};
    size_t lengths[] = {strlen(scene), strlen(monsterId), strlen(name)};
    vm_net_mock_monster_catalog_scene_battle_draft_context context;
    vm_net_mock_monster_admin_row rows[VM_NET_MOCK_MONSTER_CATALOG_MAX];
    vm_net_mock_monster_admin_row sourceTemplate;
    vm_net_mock_monster_admin_row clonedTemplate;
    int index = -1;
    u32 firstGeneratedId = 0;
    u32 secondGeneratedId = 0;
    u32 listed = 0;
    bool listedDraft = false;

    memset(&context, 0, sizeof(context));
    memset(rows, 0, sizeof(rows));
    memset(&sourceTemplate, 0, sizeof(sourceTemplate));
    memset(&clonedTemplate, 0, sizeof(clonedTemplate));
    memset(g_vm_net_mock_monster_catalog_entries, 0,
           sizeof(g_vm_net_mock_monster_catalog_entries));
    memset(g_vm_net_mock_monster_catalog_draft_only, 0,
           sizeof(g_vm_net_mock_monster_catalog_draft_only));
    memset(g_vm_net_mock_monster_resource_labels, 0,
           sizeof(g_vm_net_mock_monster_resource_labels));
    memset(g_vm_net_mock_monster_overrides, 0,
           sizeof(g_vm_net_mock_monster_overrides));
    g_vm_net_mock_monster_catalog_count = 0;
    g_vm_net_mock_monster_catalog_loaded = false;
    g_vm_net_mock_monster_catalog_loading = false;
    g_vm_net_mock_monster_db_loaded = true;
    g_vm_net_mock_monster_db_valid = false;
    vm_net_mock_monster_catalog_seed_base_entries();
    sourceTemplate.enemyId = 1000;
    sourceTemplate.level = 45;
    sourceTemplate.family = VM_NET_MOCK_MONSTER_BOSS;
    sourceTemplate.hp = 12000;
    sourceTemplate.mp = 3200;
    sourceTemplate.attack = 680;
    sourceTemplate.defense = 440;
    sourceTemplate.exp = 900;
    sourceTemplate.gold = 650;
    sourceTemplate.dropCount = 1;
    sourceTemplate.drops[0].itemId = 12;
    sourceTemplate.drops[0].rateBasisPoints = 250;

    if (!vm_net_mock_scene_battle_monster_admin_choose_monster_id(
            VM_NET_MOCK_SCENE_BATTLE_MONSTER_CUSTOM_ID_MIN - 1u,
            &firstGeneratedId, NULL) ||
        firstGeneratedId != VM_NET_MOCK_SCENE_BATTLE_MONSTER_CUSTOM_ID_MIN ||
        firstGeneratedId == 1000u ||
        !vm_net_mock_scene_battle_monster_admin_prepare_reference_clone(
            &sourceTemplate, firstGeneratedId, &clonedTemplate, NULL) ||
        sourceTemplate.enemyId != 1000u ||
        clonedTemplate.enemyId != firstGeneratedId ||
        clonedTemplate.level != sourceTemplate.level ||
        clonedTemplate.hp != sourceTemplate.hp ||
        clonedTemplate.drops[0].itemId != sourceTemplate.drops[0].itemId ||
        clonedTemplate.drops[0].rateBasisPoints !=
            sourceTemplate.drops[0].rateBasisPoints ||
        vm_net_mock_monster_catalog_add_scene_entry(firstGeneratedId) < 0 ||
        !vm_net_mock_scene_battle_monster_admin_choose_monster_id(
            VM_NET_MOCK_SCENE_BATTLE_MONSTER_CUSTOM_ID_MIN - 1u,
            &secondGeneratedId, NULL) ||
        secondGeneratedId != firstGeneratedId + 1u ||
        !vm_net_mock_monster_catalog_scene_battle_draft_row(
            &context, 3, values, lengths) ||
        context.invalid || context.rows != 1 || context.added != 1 ||
        context.rejected != 0 ||
        (index = vm_net_mock_monster_catalog_index_loaded(60000)) < 0 ||
        !g_vm_net_mock_monster_catalog_draft_only[index] ||
        strcmp(g_vm_net_mock_monster_resource_labels[index].displayName,
               name) != 0 ||
        strcmp(g_vm_net_mock_monster_resource_labels[index].firstScene,
               scene) != 0)
    {
        fputs("scene battle draft did not join the editable monster catalog\n",
              stderr);
        return false;
    }

    g_vm_net_mock_monster_catalog_loaded = true;
    listed = vm_net_mock_monster_admin_list(
        rows, VM_NET_MOCK_MONSTER_CATALOG_MAX);
    for (u32 i = 0; i < listed && i < VM_NET_MOCK_MONSTER_CATALOG_MAX; ++i)
    {
        if (rows[i].enemyId == 60000 &&
            strcmp(rows[i].displayName, name) == 0 &&
            strcmp(rows[i].firstScene, scene) == 0)
        {
            listedDraft = true;
            break;
        }
    }
    if (!listedDraft || vm_net_mock_monster_enemy_id_known(60000))
    {
        fputs("scene battle draft catalog leaked into live encounters\n", stderr);
        return false;
    }
    vm_net_mock_monster_catalog_invalidate();
    return true;
}

int main(void)
{
    char *page = (char *)calloc(VM_MOCK_ADMIN_RESPONSE_MAX, 1);

    /* Rendering this page normally asks MySQL for saved drafts. Point this
     * layout-only process at a closed local port before any server helper can
     * initialize the connection, so the regression never reads or migrates a
     * user's jh_online database. The static editor markup must render with an
     * empty draft list when that read is unavailable. */
    if (page == NULL || _putenv_s("CBE_MYSQL_HOST", "127.0.0.1") != 0 ||
        _putenv_s("CBE_MYSQL_PORT", "1") != 0 ||
        _putenv_s("CBE_MYSQL_DATABASE",
                  "cbe_scene_battle_layout_no_database") != 0 ||
        !vm_net_mock_set_resource_dir("web/fs/JHOnlineData"))
    {
        free(page);
        fprintf(stderr, "could not prepare scene battle monster layout fixture\n");
        return 1;
    }
    vm_mock_admin_render_scene_battle_monster_page(
        page, VM_MOCK_ADMIN_RESPONSE_MAX, "");
    if (strstr(page,
               ".battle-monster-fields{grid-template-columns:repeat(6,minmax(0,1fr))}") == NULL ||
        strstr(page,
               "@media(max-width:920px){.battle-monster-fields{grid-template-columns:repeat(2,minmax(0,1fr))}") == NULL ||
        strstr(page,
               "@media(max-width:560px){.battle-monster-fields{grid-template-columns:1fr}") == NULL ||
        strstr(page,
               "<div class=\"fields battle-monster-fields\"><label class=\"field battle-monster-wide\"><span>参考怪物（保存时生成新怪物 ID）</span>") == NULL ||
        strstr(page, "id=\"scene-battle-monster-search\"") == NULL ||
        strstr(page, "id=\"scene-battle-monster-list\"") == NULL ||
        strstr(page, "data-scene-battle-monster-scene=\"") == NULL ||
        strstr(page, "同一场景可保存多条") == NULL ||
        strstr(page, "保存会自动分配一个新的独立怪物 ID") == NULL ||
        strstr(page, "参考怪物（保存时生成新怪物 ID）") == NULL ||
        strstr(page, "name=\"source_monster_id\"") == NULL ||
        strstr(g_vm_mock_admin_script,
               "const setupSceneBattleMonsterSearch=()=>{") == NULL ||
        strstr(page, "<span>显示名称（GBK ≤29字节）</span>") == NULL ||
        strstr(page, "<span>Actor 资源</span>") == NULL ||
        strstr(page, "c 开头的城市会同时生成带专属 b_ 背景的非 c 战斗镜像") == NULL ||
        strstr(page,
               "@media(min-width:921px){.fields{grid-template-columns:") != NULL)
    {
        free(page);
        fprintf(stderr, "scene battle monster editor layout regressed\n");
        return 1;
    }
    if (!verify_scene_battle_draft_catalog_contract())
    {
        free(page);
        return 1;
    }
    free(page);
    puts("admin scene battle monster layout regression passed");
    return 0;
}
