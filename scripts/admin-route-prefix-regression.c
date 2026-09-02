/* Isolated regression for administrative page route prefixing.
 *
 * The renderer may emit either root-relative routes or already-prefixed
 * administrative routes.  The final page transformation must produce the
 * same routable URL in both cases, without starting a listener or touching
 * game/client state.
 */

#include <stdio.h>
#include <string.h>

#define main cbe_server_program_main
#include "../src/server_main.c"
#undef main
#include "../src/server/mock-server.c"

static int assert_admin_route_prefixing(void)
{
    char html[512] =
        "<a href=\"/accounts\">Accounts</a>"
        "<form action=\"/action\"></form>"
        "<form action=\"/admin-418yz6/action\"></form>"
        "<img src=\"/actor-preview.svg\">";
    static const char expected[] =
        "<a href=\"/admin-418yz6/accounts\">Accounts</a>"
        "<form action=\"/admin-418yz6/action\"></form>"
        "<form action=\"/admin-418yz6/action\"></form>"
        "<img src=\"/admin-418yz6/actor-preview.svg\">";

    if (!vm_mock_admin_prefix_page_routes(html, sizeof(html)))
    {
        fputs("admin page route prefixing failed\n", stderr);
        return 1;
    }
    if (strcmp(html, expected) != 0)
    {
        fprintf(stderr, "unexpected administrative routes: %s\n", html);
        return 1;
    }
    return 0;
}

static int assert_item_exchange_action_dispatch(void)
{
    if (!vm_mock_admin_action_is_npc_action("save-npc-item-exchange") ||
        !vm_mock_admin_action_is_npc_action("delete-npc-item-exchange") ||
        !vm_mock_admin_action_is_account_action("create-account") ||
        vm_mock_admin_action_is_npc_action("save-account") ||
        vm_mock_admin_action_is_npc_action(NULL) ||
        vm_mock_admin_action_is_account_action("save-account") ||
        vm_mock_admin_action_is_account_action(NULL))
    {
        fputs("administrative action whitelist is invalid\n",
              stderr);
        return 1;
    }
    return 0;
}

int main(void)
{
    if (assert_admin_route_prefixing() != 0 ||
        assert_item_exchange_action_dispatch() != 0)
        return 1;
    puts("admin route prefix regression passed");
    return 0;
}
