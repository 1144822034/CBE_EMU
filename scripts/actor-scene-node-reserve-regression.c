/* Valid editor Actor manifests must not be decoded as the client's runtime
 * motion-descriptor object. */
#include <stdio.h>

#define main cbe_server_program_main
#include "../src/server_main.c"
#undef main

int main(void)
{
    static const char *actors[] = {
        "e_ghostfireR.actor",
        "e_monkey.actor",
        "n_woman1.actor",
    };

    static const char *invalidActors[] = {
        "../e_monkey.actor",
        "e_monkey.gif",
        "e_monkey.actor/child",
    };

    for (u32 i = 0; i < sizeof(actors) / sizeof(actors[0]); ++i)
    {
        u32 reserve = 99;
        const char *error = "unexpected";
        if (!vm_net_mock_ensure_actor_resource_available(actors[i], &error))
        {
            fprintf(stderr,
                    "valid actor manifest rejected resource=%s error=%s\n",
                    actors[i], error ? error : "-");
            return 1;
        }
        error = "unexpected";
        if (!vm_net_mock_actor_scene_node_reserve(
                actors[i], &reserve, &error) || reserve != 0 || error != NULL)
        {
            fprintf(stderr,
                    "valid actor rejected resource=%s reserve=%u error=%s\n",
                    actors[i], reserve, error ? error : "-");
            return 1;
        }
    }

    for (u32 i = 0; i < sizeof(invalidActors) / sizeof(invalidActors[0]); ++i)
    {
        u32 reserve = 0;
        const char *error = NULL;
        if (vm_net_mock_actor_scene_node_reserve(
                invalidActors[i], &reserve, &error))
        {
            fprintf(stderr, "invalid actor accepted resource=%s\n",
                    invalidActors[i]);
            return 1;
        }
    }
    puts("actor scene-node reserve regression passed: valid manifests accepted; unsafe names rejected");
    return 0;
}
