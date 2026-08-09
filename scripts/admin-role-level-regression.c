/*
 * Deterministic regression for the administrative role-level contract.
 *
 * It does not start the service, open a socket, contact MySQL, or mutate an
 * account.  The server-side mutator is included directly so this verifies the
 * same level/EXP/vital normalization used by POST /action set-role-level.
 */

#include <stdio.h>
#include <string.h>

#define main cbe_server_program_main
#include "../src/main.c"
#undef main

static int expect_level(vm_net_mock_role_state *role, u32 level,
                        u32 expectedExp)
{
    if (!vm_mock_service_role_apply_admin_level(role, level) ||
        role->level != level || role->exp != expectedExp ||
        role->hpMax == 0 || role->mpMax == 0 || role->hp > role->hpMax ||
        role->mp > role->mpMax)
    {
        fprintf(stderr,
                "admin role-level contract failed level=%u actual=%u exp=%u hp=%u/%u mp=%u/%u\n",
                level, role->level, role->exp, role->hp, role->hpMax,
                role->mp, role->mpMax);
        return 1;
    }
    return 0;
}

int main(void)
{
    vm_net_mock_role_state role;

    memset(&role, 0, sizeof(role));
    vm_net_mock_role_init_default(&role);
    /* Exercise both growth and reduction.  Existing current HP/MP must be
     * clamped to a valid range rather than being treated as a free recovery. */
    role.hp = role.hpMax;
    role.mp = role.mpMax;
    if (expect_level(&role, 49, vm_net_mock_role_level_start_exp(49)) != 0 ||
        expect_level(&role, 70, vm_net_mock_role_level_start_exp(70)) != 0)
    {
        return 1;
    }
    role.hp = role.hpMax + 1;
    role.mp = role.mpMax + 1;
    if (expect_level(&role, 1, 0u) != 0)
        return 1;
    if (vm_mock_service_role_apply_admin_level(&role, 0) ||
        vm_mock_service_role_apply_admin_level(
            &role, VM_NET_MOCK_ROLE_LEVEL_CAP + 1u))
    {
        fputs("admin role-level contract accepted an out-of-range level\n",
              stderr);
        return 1;
    }

    puts("admin role-level regression passed: level/EXP/vitals contract");
    return 0;
}
