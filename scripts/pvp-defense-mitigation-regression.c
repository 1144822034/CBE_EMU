/* Deterministic regression for the PvP-only armour mitigation curve.
 *
 * It includes the production mock service directly, starts no listener, and
 * performs no database or client I/O.  The checks cover only the reviewed
 * 75% cap curve; the separately versioned PvE formula has its own regression.
 */

#include <stdint.h>
#include <stdio.h>

#define main cbe_server_program_main
#include "../src/server_main.c"
#undef main
#include "../src/server/mock-server.c"

typedef struct
{
    u32 rawDamage;
    u32 defense;
    u32 expectedDamage;
} pvp_defense_case;

static int assert_pvp_defense_mitigation(void)
{
    static const pvp_defense_case cases[] = {
        {1000u, 0u, 1000u},
        {1000u, 1000u, 750u},
        {1000u, 3000u, 550u},
        {1000u, 6000u, 438u},
        {1000u, 9300u, 383u},
        {1000u, 34500u, 291u},
        {1000u, UINT32_MAX, 250u},
        {1u, UINT32_MAX, 1u},
    };

    for (u32 i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i)
    {
        u32 actual = vm_mock_service_duel_damage_after_pvp_defense(
            cases[i].rawDamage, cases[i].defense);

        if (actual != cases[i].expectedDamage)
        {
            fprintf(stderr,
                    "pvp mitigation mismatch raw=%u defense=%u actual=%u expected=%u\n",
                    cases[i].rawDamage, cases[i].defense, actual,
                    cases[i].expectedDamage);
            return 1;
        }
    }

    return 0;
}

int main(void)
{
    if (assert_pvp_defense_mitigation() != 0)
        return 1;
    puts("pvp defense mitigation regression passed");
    return 0;
}
