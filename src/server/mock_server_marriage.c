/*
 * Pixi marriage helpers: spouse teleport WT 1/32.
 * Included from mock-server.c after social (scene enter helpers available).
 */

enum
{
    VM_NET_MOCK_MARRIAGE_WT_KIND = 32,
    VM_NET_MOCK_MARRIAGE_SUBTYPE_TELEPORT = 1,
    VM_NET_MOCK_MARRIAGE_SUBTYPE_TELEPORT_ACK = 2
};

static bool vm_net_mock_is_marriage32_request(const u8 *request, u32 requestLen,
                                              u8 *subtypeOut)
{
    u32 offset = 4;
    vm_net_mock_request_object object;
    u8 subtype = 0;

    if (subtypeOut)
        *subtypeOut = 0;
    if (request == NULL || requestLen < 9 || request[0] != 'W' || request[1] != 'T')
        return false;
    if (!vm_net_mock_next_request_object(request, requestLen, &offset, &object))
        return false;
    if (object.major != 1 || object.kind != VM_NET_MOCK_MARRIAGE_WT_KIND ||
        object.subtype != VM_NET_MOCK_MARRIAGE_SUBTYPE_TELEPORT)
        return false;
    subtype = object.subtype;
    if (vm_net_mock_next_request_object(request, requestLen, &offset, &object))
        return false;
    if (subtypeOut)
        *subtypeOut = subtype;
    return offset == requestLen;
}

static u32 vm_net_mock_build_marriage_teleport_ack(u8 *out, u32 outCap, u8 result,
                                                   const char *tip)
{
    u32 pos = 5;
    u32 objectStart = 0;

    if (out == NULL || outCap < 16)
        return 0;
    if (!vm_net_mock_begin_wt_object(out, outCap, &pos, 1,
                                     VM_NET_MOCK_MARRIAGE_WT_KIND,
                                     VM_NET_MOCK_MARRIAGE_SUBTYPE_TELEPORT_ACK,
                                     &objectStart) ||
        !vm_net_mock_put_object_u8(out, outCap, &pos, "result", result))
        return 0;
    if (tip != NULL && tip[0] != 0)
    {
        if (!vm_net_mock_put_object_string(out, outCap, &pos, "tip", tip))
            return 0;
    }
    vm_net_mock_finish_wt_object(out, objectStart, pos);
    out[0] = 'W';
    out[1] = 'T';
    out[2] = (u8)(pos >> 8);
    out[3] = (u8)pos;
    out[4] = 1;
    return pos;
}

static u32 vm_net_mock_build_marriage32_response(const u8 *request, u32 requestLen,
                                                 u8 *out, u32 outCap)
{
    u8 subtype = 0;
    vm_net_mock_role_state *role;
    vm_mock_service_client_session *self;
    vm_mock_service_client_session *spouse;
    vm_mock_service_client_session *cand;

    if (!vm_net_mock_is_marriage32_request(request, requestLen, &subtype))
        return 0;
    role = vm_net_mock_active_role();
    self = vm_mock_service_get_active_client_session();
    if (role == NULL || self == NULL)
        return vm_net_mock_build_marriage_teleport_ack(
            out, outCap, 0, "\xb4\xab\xcb\xcd\xca\xa7\xb0\xdc"); /* 传送失败 */

    if (role->spouseRoleId == 0)
    {
        return vm_net_mock_build_marriage_teleport_ack(
            out, outCap, 0,
            "\xc4\xe3\xc9\xd0\xd3\xd0\xc5\xe4\xc5\xb6"); /* 你尚无配偶 */
    }

    spouse = NULL;
    for (cand = g_vm_mock_service_client_sessions; cand != NULL; cand = cand->next)
    {
        if (cand->onlineRoleId == role->spouseRoleId && cand->roleOnline &&
            cand->accountId[0] != 0)
        {
            spouse = cand;
            break;
        }
    }
    if (spouse == NULL)
    {
        return vm_net_mock_build_marriage_teleport_ack(
            out, outCap, 0,
            "\xc5\xe4\xc5\xb6\xb5\xb1\xc7\xb0\xc0\xeb\xcf\xdf"); /* 配偶当前离线 */
    }

    {
        const char *scene =
            spouse->sceneVisibleScene[0] ? spouse->sceneVisibleScene
                                         : (spouse->onlineScene[0] ? spouse->onlineScene
                                                                   : NULL);
        u16 x = spouse->sceneVisibleX ? spouse->sceneVisibleX : spouse->onlineX;
        u16 y = spouse->sceneVisibleY ? spouse->sceneVisibleY : spouse->onlineY;
        vm_net_mock_scene_change_target target;
        u32 pos = 5;

        if (scene == NULL || scene[0] == 0)
        {
            return vm_net_mock_build_marriage_teleport_ack(
                out, outCap, 0, "\xb4\xab\xcb\xcd\xca\xa7\xb0\xdc");
        }
        if (x == 0)
            x = 120;
        if (y == 0)
            y = 120;
        memset(&target, 0, sizeof(target));
        snprintf(target.scene, sizeof(target.scene), "%s", scene);
        target.x = x;
        target.y = y;
        target.mapType = 2;
        target.hasSceEntry = true;
        if (!vm_net_mock_append_scene_enter_object_for_scene(out, outCap, &pos,
                                                              scene, x, y))
        {
            return vm_net_mock_build_marriage_teleport_ack(
                out, outCap, 0, "\xb4\xab\xcb\xcd\xca\xa7\xb0\xdc");
        }
        vm_mock_service_mark_active_session_scene_pending(&target,
                                                         "spouse-teleport");
        out[0] = 'W';
        out[1] = 'T';
        out[2] = (u8)(pos >> 8);
        out[3] = (u8)pos;
        out[4] = 1;
        printf("[info][network] mock_spouse_teleport role=%u -> spouse=%u "
               "scene=%s pos=(%u,%u)\n",
               role->roleId, spouse->onlineRoleId, scene, x, y);
        return pos;
    }
}
