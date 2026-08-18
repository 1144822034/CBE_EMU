/*
 * Pure regression for skill-trainer list ownership.  It does not open a
 * listener, connect to MySQL, mutate a live role, or run a client binary.
 */

#include <stdio.h>
#include <string.h>

#define main cbe_server_program_main
#include "../src/server_main.c"
#undef main

static int expect(bool condition, const char *message)
{
    if (condition)
        return 0;
    fprintf(stderr, "npc skill trainer pagination regression: %s\n", message);
    return 1;
}

int main(void)
{
    vm_net_mock_role_state role;
    vm_net_mock_role_service_state firstRoleSkills;
    vm_net_mock_role_service_state secondRoleSkills;
    vm_mock_service_client_session session;
    vm_mock_service_npc_context serviceContext;
    vm_mock_service_npc_transaction_context transaction;

    memset(&role, 0, sizeof(role));
    memset(&firstRoleSkills, 0, sizeof(firstRoleSkills));
    memset(&secondRoleSkills, 0, sizeof(secondRoleSkills));
    memset(&session, 0, sizeof(session));
    memset(&serviceContext, 0, sizeof(serviceContext));
    memset(&transaction, 0, sizeof(transaction));
    memset(g_vm_net_mock_skill_catalog, 0,
           sizeof(g_vm_net_mock_skill_catalog));

    role.roleId = 7001;
    role.job = 1;
    role.level = 10;
    firstRoleSkills.roleId = role.roleId;
    firstRoleSkills.learnedSkillCount = 7;
    for (u32 i = 0; i < firstRoleSkills.learnedSkillCount; ++i)
        firstRoleSkills.learnedSkillIds[i] = i + 1u;

    secondRoleSkills.roleId = 7002;
    secondRoleSkills.learnedSkillCount = 1;
    secondRoleSkills.learnedSkillIds[0] = 1;
    serviceContext.active = true;
    serviceContext.roleId = role.roleId;
    serviceContext.actorId = 21001;
    serviceContext.serviceMask = vm_net_mock_npc_service_kind_mask(
        VM_NET_MOCK_NPC_KIND_SKILL_TRAINER);
    snprintf(serviceContext.scene, sizeof(serviceContext.scene), "%s",
             "\x30\x30\xc5\xee\xc0\xb3\xcf\xc9\xb5\xba\x5f\x30\x32\x2e\x73\x63\x65"); /* 00蓬莱仙岛_02.sce */

    g_vm_net_mock_skill_catalog_loaded = true;
    g_vm_net_mock_skill_catalog_count = 15;
    for (u32 i = 0; i < 14; ++i)
    {
        g_vm_net_mock_skill_catalog[i].skillId = i + 1u;
        g_vm_net_mock_skill_catalog[i].rawJob = 0;
        g_vm_net_mock_skill_catalog[i].levelRequired = i == 0 ? 1 : 5;
        g_vm_net_mock_skill_catalog[i].learnPrice = 100u + i;
        snprintf(g_vm_net_mock_skill_catalog[i].name,
                 sizeof(g_vm_net_mock_skill_catalog[i].name), "skill%u",
                 i + 1u);
    }
    g_vm_net_mock_skill_catalog[14].skillId = 101;
    g_vm_net_mock_skill_catalog[14].rawJob = 1;
    g_vm_net_mock_skill_catalog[14].levelRequired = 1;

    if (expect(vm_net_mock_npc_skill_list_total(
                   &role, &firstRoleSkills, false) == 7,
               "learn list did not include all seven eligible unlearned skills") ||
        expect(vm_net_mock_npc_skill_list_item_page(
                   &role, &firstRoleSkills, false, 13) == 1,
               "sixth learnable skill was not placed on page two") ||
        expect(vm_net_mock_npc_skill_list_total(
                   &role, &firstRoleSkills, true) == 6,
               "forget list did not include the six learned non-starter skills") ||
        expect(vm_net_mock_npc_skill_list_item_page(
                   &role, &firstRoleSkills, true, 7) == 1,
               "sixth forgettable skill was not placed on page two") ||
        expect(!vm_net_mock_npc_skill_list_matches(
                   &g_vm_net_mock_skill_catalog[0], &role,
                   &firstRoleSkills, true),
               "starter skill was exposed as forgettable") ||
        expect(!vm_net_mock_npc_skill_list_matches(
                   &g_vm_net_mock_skill_catalog[14], &role,
                   &firstRoleSkills, false),
               "another profession's skill entered the learn list") ||
        expect(vm_net_mock_npc_skill_list_total(
                   &role, &secondRoleSkills, false) == 13,
               "a second role inherited the first role's learned-skill filter") ||
        expect(vm_net_mock_npc_skill_list_total(
                   &role, &secondRoleSkills, true) == 0,
               "starter-only role exposed a forget option") ||
        expect(vm_net_mock_npc_skill_list_clamp_page(6, 9) == 1,
               "shrunk two-page list did not clamp to its last page") ||
        expect(vm_net_mock_npc_skill_list_clamp_page(5, 1) == 0,
               "single-page list retained an invalid second page") ||
        expect(vm_net_mock_npc_service_opcode_is_supported(
                   VM_NET_MOCK_NPC_SERVICE_OPEN_SKILL_LEARN_BASE),
               "learn-list opcode is rejected by the service detector") ||
        expect(vm_net_mock_npc_service_opcode_is_supported(
                   VM_NET_MOCK_NPC_SERVICE_OPEN_SKILL_FORGET_BASE),
               "forget-list opcode is rejected by the service detector") ||
        expect(vm_net_mock_npc_service_opcode_is_supported(
                   VM_NET_MOCK_NPC_SERVICE_FORGET_SKILL_BASE),
               "forget-action opcode is rejected by the service detector") ||
        expect(!vm_net_mock_npc_service_opcode_is_supported(0xf5000000u),
               "unknown adjacent service opcode was accepted") ||
        expect(vm_net_mock_npc_transaction_context_begin(
                   &session, &role, &serviceContext,
                   VM_MOCK_SERVICE_NPC_TRANSACTION_SKILL_LEARN,
                   13, 0, 0, 1, 112),
               "learn prompt did not create a confirmation context") ||
        expect(session.npcTransactionContext.active &&
                   session.npcTransactionContext.itemId == 13 &&
                   session.npcTransactionContext.page == 1 &&
                   session.npcTransactionContext.quotedPrice == 112,
               "learn confirmation context lost skill, page, or price") ||
        expect(vm_net_mock_npc_transaction_context_take(
                   &session, &role, &serviceContext, &transaction) &&
                   transaction.kind ==
                       VM_MOCK_SERVICE_NPC_TRANSACTION_SKILL_LEARN,
               "learn confirmation did not consume its context") ||
        expect(!vm_net_mock_npc_transaction_context_take(
                   &session, &role, &serviceContext, &transaction),
               "learn confirmation context can be replayed") ||
        expect(vm_net_mock_npc_transaction_context_begin(
                   &session, &role, &serviceContext,
                   VM_MOCK_SERVICE_NPC_TRANSACTION_SKILL_FORGET,
                   7, 0, 0, 1, 106),
               "forget prompt did not create a confirmation context") ||
        expect(VM_NET_MOCK_NPC_SERVICE_SKILL_PAGE_ITEMS + 2u <=
                   VM_NET_MOCK_NPC_SERVICE_DIALOG_MAX_OPTIONS,
               "five rows plus pager controls exceed the dialog option bound"))
    {
        return 1;
    }

    ++serviceContext.actorId;
    if (expect(!vm_net_mock_npc_transaction_context_take(
                   &session, &role, &serviceContext, &transaction) &&
                   !session.npcTransactionContext.active,
               "forget confirmation accepted a mismatched NPC or retained the context"))
    {
        return 1;
    }

    puts("npc skill trainer pagination regression passed: split lists, paging, confirmation contexts, starter protection, and role isolation");
    return 0;
}
