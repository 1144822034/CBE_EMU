#include "mock_server.h"

/*
 * Native 比武擂台大厅 support.
 *
 * The CBE task-hall screen owns the UI state.  This module only speaks the
 * decoder-confirmed request/response contract documented in
 * docs/re/2026-08-12-arena-lobby.md; it never forces a screen or reuses the
 * ordinary nearby-player spar protocol as an arena substitute.
 */

enum
{
    VM_NET_MOCK_ARENA_ROOM_MAX = 24,
    VM_NET_MOCK_ARENA_ROOM_MEMBER_MAX = 2,
    VM_NET_MOCK_ARENA_ROOM_PAGE_SIZE = 10,
    VM_NET_MOCK_ARENA_MAX_AWARD = 10000,
    VM_NET_MOCK_ARENA_MAX_TURNS = 20,
    VM_NET_MOCK_ARENA_VITALITY_PER_TURN = 5,
    VM_NET_MOCK_ARENA_AWARD_STEP_LOW = 100,
    VM_NET_MOCK_ARENA_AWARD_STEP_MID = 500,
    VM_NET_MOCK_ARENA_AWARD_STEP_HIGH = 1000
};

typedef struct
{
    u32 roleId;
    char name[32];
    u8 job;
    u16 level;
} vm_net_mock_arena_room_member;

typedef struct
{
    bool active;
    u32 roomId;
    u8 type;
    u16 award;
    u8 turns;
    u8 memberCount;
    u8 completedRounds;
    bool challengePending;
    u32 challengeRoleId;
    u32 challengeOpponentRoleId;
    u32 challengeTick;
    u32 activeDuelSerial;
    u32 createdTick;
    vm_net_mock_arena_room_member members[VM_NET_MOCK_ARENA_ROOM_MEMBER_MAX];
} vm_net_mock_arena_room;

static vm_net_mock_arena_room
    g_vm_net_mock_arena_rooms[VM_NET_MOCK_ARENA_ROOM_MAX];
static u32 g_vm_net_mock_arena_next_room_id = 1;

enum
{
    VM_NET_MOCK_ARENA_CHALLENGE_TIMEOUT_TICKS =
        60u * 1000u / VM_SCHED_FRAME_MS
};

static bool vm_net_mock_arena_request_object(const u8 *request, u32 requestLen,
                                             u8 subtype,
                                             vm_net_mock_request_object *objectOut)
{
    u32 offset = 4;
    vm_net_mock_request_object object;

    if (request == NULL || requestLen < 9 || request[0] != 'W' ||
        request[1] != 'T' || request[4] != 1 ||
        !vm_net_mock_next_request_object(request, requestLen, &offset, &object) ||
        offset != requestLen || object.major != 1 || object.kind != 30 ||
        object.subtype != subtype)
    {
        return false;
    }
    if (objectOut)
        *objectOut = object;
    return true;
}

static vm_mock_service_client_session *
vm_net_mock_arena_find_online_session(u32 roleId)
{
    return vm_mock_service_find_online_session_by_role_id(roleId);
}

/* Arena rooms are a live activity, just like team membership.  Do not leave
 * phantom rooms after the host disconnects or after a joined challenger is no
 * longer online. */
static void vm_net_mock_arena_prune_rooms(void)
{
    for (u32 i = 0; i < VM_NET_MOCK_ARENA_ROOM_MAX; ++i)
    {
        vm_net_mock_arena_room *room = &g_vm_net_mock_arena_rooms[i];
        u8 writeIndex = 0;

        if (!room->active)
            continue;
        for (u8 readIndex = 0; readIndex < room->memberCount; ++readIndex)
        {
            vm_net_mock_arena_room_member *member = &room->members[readIndex];
            vm_mock_service_client_session *session =
                vm_net_mock_arena_find_online_session(member->roleId);
            vm_mock_service_online_session_view sessionView;

            if (session == NULL ||
                !vm_mock_service_session_get_online_view(session, &sessionView))
                continue;
            if (writeIndex != readIndex)
                room->members[writeIndex] = room->members[readIndex];
            room->members[writeIndex].job = sessionView.onlineJob;
            room->members[writeIndex].level = sessionView.onlineLevel;
            if (sessionView.onlineRoleName[0] != 0)
                snprintf(room->members[writeIndex].name,
                         sizeof(room->members[writeIndex].name), "%s",
                         sessionView.onlineRoleName);
            ++writeIndex;
        }
        room->memberCount = writeIndex;
        /* The first member is always the host.  A room without it must be
         * discarded rather than promoted to an unrelated challenger. */
        if (room->memberCount == 0 ||
            vm_net_mock_arena_find_online_session(room->members[0].roleId) == NULL)
        {
            memset(room, 0, sizeof(*room));
        }
        else if (room->challengePending &&
                 (room->memberCount != VM_NET_MOCK_ARENA_ROOM_MEMBER_MAX ||
                  g_schedulerTick - room->challengeTick >
                      VM_NET_MOCK_ARENA_CHALLENGE_TIMEOUT_TICKS))
        {
            room->challengePending = false;
            room->challengeRoleId = 0;
            room->challengeOpponentRoleId = 0;
            room->challengeTick = 0;
        }
    }
}

static vm_net_mock_arena_room *vm_net_mock_arena_find_room(u32 roomId)
{
    if (roomId == 0)
        return NULL;
    for (u32 i = 0; i < VM_NET_MOCK_ARENA_ROOM_MAX; ++i)
    {
        if (g_vm_net_mock_arena_rooms[i].active &&
            g_vm_net_mock_arena_rooms[i].roomId == roomId)
        {
            return &g_vm_net_mock_arena_rooms[i];
        }
    }
    return NULL;
}

static int vm_net_mock_arena_member_index(const vm_net_mock_arena_room *room,
                                          u32 roleId)
{
    if (room == NULL || !room->active || roleId == 0)
        return -1;
    for (u8 i = 0; i < room->memberCount; ++i)
    {
        if (room->members[i].roleId == roleId)
            return (int)i;
    }
    return -1;
}

static void vm_net_mock_arena_clear_challenge(vm_net_mock_arena_room *room)
{
    if (room == NULL)
        return;
    room->challengePending = false;
    room->challengeRoleId = 0;
    room->challengeOpponentRoleId = 0;
    room->challengeTick = 0;
}

static bool vm_net_mock_arena_append_challenge_prompt_object(
    u8 *out, u32 outCap, u32 *pos, const vm_net_mock_arena_room *room,
    u32 challengerRoleId)
{
    u32 ackObjectStart = 0;
    u32 promptObjectStart = 0;
    char prompt[128];
    const char *opponentName = NULL;
    int challengerIndex = -1;

    if (out == NULL || pos == NULL || room == NULL || room->memberCount != 2)
        return false;
    challengerIndex = vm_net_mock_arena_member_index(room, challengerRoleId);
    if (challengerIndex < 0)
        return false;
    opponentName = room->members[1 - challengerIndex].name[0] ?
                       room->members[1 - challengerIndex].name : "Player";
    snprintf(prompt, sizeof(prompt),
             "\xca\xc7\xb7\xf1\xcf\xf2%s\xb7\xa2\xc6\xf0\xb1\xc8\xce\xe4\xcc\xf4\xd5\xbd\xa3\xbf", /* 是否向%s发起比武挑战？ */
             opponentName);

    /* The 30/9 parser only installs the confirmation callbacks.  It does not
     * clear the task-hall request progress layer.  DispatchItemEvent
     * (0x01039C28) clears that layer after dispatching a kind-26 object, so
     * acknowledge the current room action with its harmless 26/0 object
     * before the native challenge prompt.  This is the same verified
     * clear-then-prompt ordering used by the instance challenge flow. */
    if (!vm_net_mock_begin_wt_object(out, outCap, pos, 1, 26, 0,
                                     &ackObjectStart))
    {
        return false;
    }
    vm_net_mock_finish_wt_object(out, ackObjectStart, *pos);

    if (!vm_net_mock_begin_wt_object(out, outCap, pos, 1, 30, 9,
                                     &promptObjectStart) ||
        !vm_net_mock_put_object_u8(out, outCap, pos, "isleader", 0) ||
        !vm_net_mock_put_object_string(out, outCap, pos, "challenge", prompt))
    {
        return false;
    }
    vm_net_mock_finish_wt_object(out, promptObjectStart, *pos);
    return true;
}

static vm_mock_service_duel *vm_net_mock_arena_begin_duel(
    vm_net_mock_arena_room *room, u32 challengerRoleId)
{
    int challengerIndex = vm_net_mock_arena_member_index(room,
                                                          challengerRoleId);
    vm_mock_service_client_session *challenger = NULL;
    vm_mock_service_client_session *opponent = NULL;
    vm_mock_service_duel *duel = NULL;

    if (room == NULL || !room->active ||
        room->memberCount != VM_NET_MOCK_ARENA_ROOM_MEMBER_MAX ||
        room->activeDuelSerial != 0 || challengerIndex < 0)
    {
        return NULL;
    }
    challenger = vm_net_mock_arena_find_online_session(challengerRoleId);
    opponent = vm_net_mock_arena_find_online_session(
        room->members[1 - challengerIndex].roleId);
    if (challenger == NULL || opponent == NULL)
        return NULL;
    duel = vm_mock_service_arena_duel_begin(challenger, opponent, room->roomId);
    if (duel != NULL)
        room->activeDuelSerial = vm_mock_service_duel_serial(duel);
    return duel;
}

static bool vm_net_mock_arena_queue_opponent_challenge(
    vm_net_mock_arena_room *room,
    vm_mock_service_client_session *challenger,
    const vm_net_mock_role_state *challengerRole,
    const char *challengerAccountId)
{
    vm_mock_service_client_session *opponent = NULL;
    vm_mock_service_online_session_view challengerView;
    vm_mock_service_online_session_view opponentView;
    int challengerIndex = -1;

    if (room == NULL || challenger == NULL || challengerRole == NULL ||
        room->memberCount != VM_NET_MOCK_ARENA_ROOM_MEMBER_MAX)
    {
        return false;
    }
    challengerIndex = vm_net_mock_arena_member_index(room,
                                                       challengerRole->roleId);
    if (challengerIndex < 0)
        return false;
    opponent = vm_net_mock_arena_find_online_session(
        room->members[1 - challengerIndex].roleId);
    if (!vm_mock_service_session_get_online_view(challenger, &challengerView) ||
        !vm_mock_service_session_get_online_view(opponent, &opponentView) ||
        opponentView.clientId == challengerView.clientId ||
        !opponentView.sceneVisibleReady)
    {
        return false;
    }
    /* Arena challenge is a task-hall/scene-channel protocol of its own.
     * Its parser displays 30/9 and replies with 30/10 {agree}; emitting the
     * nearby-player 4/15 invitation here opens an unrelated "切磋" dialog and
     * diverts the reply into the ordinary spar state machine. */
    return vm_mock_service_session_enqueue_arena_challenge_notice(
        opponent, challenger, challengerRole, challengerAccountId);
}

static bool vm_net_mock_arena_is_confirm_request(const u8 *request,
                                                  u32 requestLen, u8 *agreeOut)
{
    vm_net_mock_request_object object;
    vm_net_mock_request_object extra;
    u32 offset = 4;
    u8 agree = 0;

    if (agreeOut)
        *agreeOut = 0;
    if (request == NULL || requestLen != 20 ||
        !vm_net_mock_next_request_object(request, requestLen, &offset, &object) ||
        object.major != 1 || object.kind != 30 || object.subtype != 10 ||
        object.payloadLen != 11 ||
        !vm_net_mock_get_object_u8_field(object.payload, object.payloadLen,
                                         "agree", &agree) ||
        vm_net_mock_next_request_object(request, requestLen, &offset, &extra) ||
        offset != requestLen)
    {
        return false;
    }
    if (agreeOut)
        *agreeOut = agree;
    return true;
}

static bool vm_net_mock_arena_append_confirm_ack_object(
    u8 *out, u32 outCap, u32 *pos, const char *notify)
{
    u32 objectStart = 0;

    /*
     * 30/10 is not a passive acknowledgement in the task-hall client.
     * net_handle_scene_channel_dispatch routes it to 0x01039528: result=0
     * only clears the request-pending marker, while result=1 consumes the
     * failnotify text and calls CleanupTaskHall (0x010491AE).  The arena
     * confirmation originates from task-hall mode 31, so using result=0
     * leaves that screen active and makes a later 4/10 battle start inert.
     *
     * Keep the native result=1 envelope and provide the field its reader
     * requires.  The text is rendered as a transient native notification;
     * it is not a scene transfer and does not alter the character position.
     */
    if (!vm_net_mock_begin_wt_object(out, outCap, pos, 1, 30, 10,
                                     &objectStart) ||
        !vm_net_mock_put_object_u8(out, outCap, pos, "result", 1) ||
        !vm_net_mock_put_object_string(
            out, outCap, pos, "failnotify",
            notify ? notify : "\xb1\xc8\xce\xe4\xcc\xf4\xd5\xbd\xd2\xd1\xc8\xb7\xc8\xcf")) /* 比武挑战已确认 */
    {
        return false;
    }
    vm_net_mock_finish_wt_object(out, objectStart, *pos);
    return true;
}

/* A remote 30/9 challenge is delivered while the target still owns the scene,
 * rather than the task-hall screen.  Its 30/10 reply must clear that request's
 * pending layer without running CleanupTaskHall. */
static bool vm_net_mock_arena_append_confirm_pending_ack_object(
    u8 *out, u32 outCap, u32 *pos)
{
    u32 objectStart = 0;

    if (!vm_net_mock_begin_wt_object(out, outCap, pos, 1, 30, 10,
                                     &objectStart) ||
        !vm_net_mock_put_object_u8(out, outCap, pos, "result", 0))
    {
        return false;
    }
    vm_net_mock_finish_wt_object(out, objectStart, *pos);
    return true;
}

/* The room-list screen owns the second 30/8 request, but it cannot own the
 * native 30/9 -> 30/10 confirmation callback that starts a battle module.
 * Close that screen first using the same 26/0 + result=1 task-hall sequence
 * used by established task-hall flows; the actual challenge prompt is emitted
 * by the following scene poll. */
static bool vm_net_mock_arena_append_prepare_scene_confirm_objects(
    u8 *out, u32 outCap, u32 *pos)
{
    u32 objectStart = 0;

    if (!vm_net_mock_begin_wt_object(out, outCap, pos, 1, 26, 0,
                                     &objectStart))
    {
        return false;
    }
    vm_net_mock_finish_wt_object(out, objectStart, *pos);
    return vm_net_mock_arena_append_confirm_ack_object(
        out, outCap, pos,
        "\xd5\xfd\xd4\xda\xd7\xbc\xb1\xb8\xb1\xc8\xce\xe4\xcc\xf4\xd5\xbd\xa1\xa3"); /* 正在准备比武挑战。 */
}

/* Emit the initiator's 30/9 only after task-hall has been closed and the
 * scene owns the callback.  This reproduces the proven instance-challenge
 * order (26/0 -> 30/9 -> 30/10 {agree} -> 4/10), instead of treating a
 * room-list confirmation as a battle-entry event. */
u32 vm_net_mock_build_pending_arena_initiator_confirm_response(
    u8 *out, u32 outCap, vm_mock_service_client_session *observer)
{
    vm_net_mock_arena_room *room = NULL;
    vm_mock_service_online_session_view observerView;
    u32 pos = 5;

    if (out == NULL || observer == NULL ||
        !vm_mock_service_session_get_online_view(observer, &observerView) ||
        !observerView.roleOnline || observerView.onlineRoleId == 0 ||
        !observerView.arenaChallengeInitiatorPromptPending ||
        observerView.arenaChallengeReplyActive ||
        !observerView.sceneVisibleReady || observerView.sceneVisiblePending)
    {
        return 0;
    }
    vm_net_mock_arena_prune_rooms();
    for (u32 i = 0; i < VM_NET_MOCK_ARENA_ROOM_MAX; ++i)
    {
        vm_net_mock_arena_room *candidate = &g_vm_net_mock_arena_rooms[i];

        if (candidate->active && candidate->challengePending &&
            candidate->challengeRoleId == observerView.onlineRoleId &&
            candidate->challengeOpponentRoleId == 0 &&
            candidate->activeDuelSerial == 0)
        {
            room = candidate;
            break;
        }
    }
    if (room == NULL)
    {
        vm_mock_service_session_set_arena_challenge_state(
            observer, false, observerView.arenaChallengeReplyActive,
            observerView.arenaChallengeSourceRoleId);
        return 0;
    }
    if (!vm_net_mock_arena_append_challenge_prompt_object(
            out, outCap, &pos, room, observerView.onlineRoleId))
    {
        return 0;
    }
    vm_net_mock_finish_wt_packet(out, pos, 2);
    vm_mock_service_session_set_arena_challenge_state(
        observer, false, true, observerView.onlineRoleId);
    printf("[info][mock-service] arena_initiator_challenge_notice_deliver observer=%08x room=%u role=%u stage=scene-native-prompt resp=%u\n",
           observerView.clientId, room->roomId, observerView.onlineRoleId, pos);
    return pos;
}

/* The released-duel edge occurs only after both battle clients consumed their
 * terminal packets.  This is the only point at which an arena round is
 * counted as complete, so stale polls and one-sided disconnects cannot make
 * a room silently advance or close. */
void vm_net_mock_arena_on_duel_released(u32 roomId, u32 duelSerial)
{
    vm_net_mock_arena_room *room = vm_net_mock_arena_find_room(roomId);

    if (room == NULL || room->activeDuelSerial != duelSerial)
        return;
    room->activeDuelSerial = 0;
    ++room->completedRounds;
    if (room->completedRounds >= room->turns)
    {
        printf("[info][mock-service] arena_room_complete room=%u duel=%u rounds=%u/%u action=close\n",
               room->roomId, duelSerial, room->completedRounds, room->turns);
        memset(room, 0, sizeof(*room));
        return;
    }
    printf("[info][mock-service] arena_round_complete room=%u duel=%u rounds=%u/%u action=await-next-challenge\n",
           room->roomId, duelSerial, room->completedRounds, room->turns);
}

static vm_net_mock_arena_room *vm_net_mock_arena_allocate_room(void)
{
    for (u32 i = 0; i < VM_NET_MOCK_ARENA_ROOM_MAX; ++i)
    {
        if (!g_vm_net_mock_arena_rooms[i].active)
            return &g_vm_net_mock_arena_rooms[i];
    }
    return NULL;
}

void vm_net_mock_arena_remove_role(u32 roleId, const char *reason)
{
    if (roleId == 0)
        return;
    for (u32 i = 0; i < VM_NET_MOCK_ARENA_ROOM_MAX; ++i)
    {
        vm_net_mock_arena_room *room = &g_vm_net_mock_arena_rooms[i];

        if (!room->active)
            continue;
        for (u8 memberIndex = 0; memberIndex < room->memberCount;
             ++memberIndex)
        {
            if (room->members[memberIndex].roleId != roleId)
                continue;
            if (memberIndex == 0)
            {
                printf("[info][mock-service] arena_room_close room=%u host=%u reason=%s\n",
                       room->roomId, roleId, reason ? reason : "offline");
                memset(room, 0, sizeof(*room));
            }
            else
            {
                --room->memberCount;
                memset(&room->members[room->memberCount], 0,
                       sizeof(room->members[room->memberCount]));
                printf("[info][mock-service] arena_room_leave room=%u role=%u reason=%s\n",
                       room->roomId, roleId, reason ? reason : "offline");
            }
            break;
        }
    }
}

static bool vm_net_mock_arena_context_is_active(
    const vm_net_mock_role_state *role,
    const vm_mock_service_client_session *session)
{
    const vm_mock_service_npc_context *context =
        vm_net_mock_npc_service_context_get(session, role);

    return vm_net_mock_npc_service_context_has(
        context, VM_NET_MOCK_NPC_KIND_ARENA_MASTER);
}

static bool vm_net_mock_arena_append_open_result_object(u8 *out, u32 outCap,
                                                        u32 *pos)
{
    u32 objectStart = 0;

    if (!vm_net_mock_begin_wt_object(out, outCap, pos, 1, 26, 1,
                                     &objectStart) ||
        !vm_net_mock_put_object_u8(out, outCap, pos, "result", 1))
    {
        return false;
    }
    vm_net_mock_finish_wt_object(out, objectStart, *pos);
    return true;
}

static bool vm_net_mock_arena_append_config_object(u8 *out, u32 outCap,
                                                    u32 *pos)
{
    u32 objectStart = 0;

    /* `turnvity` is the exact spelling consumed by ParseArenaRewardData.
     * These are lobby constraints only; no vitality, wager or reward is
     * mutated until the still-unreversed arena battle confirmation exists. */
    if (!vm_net_mock_begin_wt_object(out, outCap, pos, 1, 30, 4,
                                     &objectStart) ||
        /* ParseArenaRewardData calls LookupItemIntField (not
         * LookupItemShortField) for these five values.  That reader starts at
         * value[2] and consumes four bytes, so each must be a typed BE32
         * object value: 00 04 xx xx xx xx. */
        !vm_net_mock_put_object_u32(out, outCap, pos, "turnvity",
                                    VM_NET_MOCK_ARENA_VITALITY_PER_TURN) ||
        !vm_net_mock_put_object_u32(out, outCap, pos, "minaward",
                                    VM_NET_MOCK_ARENA_AWARD_STEP_LOW) ||
        !vm_net_mock_put_object_u32(out, outCap, pos, "minaward2",
                                    VM_NET_MOCK_ARENA_AWARD_STEP_MID) ||
        !vm_net_mock_put_object_u32(out, outCap, pos, "minaward3",
                                    VM_NET_MOCK_ARENA_AWARD_STEP_HIGH) ||
        !vm_net_mock_put_object_u32(out, outCap, pos, "maxaward",
                                    VM_NET_MOCK_ARENA_MAX_AWARD) ||
        /* maxturn alone is read by LookupItemByteField, thus 00 01 xx. */
        !vm_net_mock_put_object_u8(out, outCap, pos, "maxturn",
                                   VM_NET_MOCK_ARENA_MAX_TURNS))
    {
        return false;
    }
    vm_net_mock_finish_wt_object(out, objectStart, *pos);
    return true;
}

static bool vm_net_mock_arena_append_room_list_object(u8 *out, u32 outCap,
                                                       u32 *pos)
{
    u8 roomList[1024];
    u8 colNames[96];
    u32 roomListLen = 0;
    u32 colNamesLen = 0;
    u8 roomCount = 0;
    u32 objectStart = 0;
    /* The client renders the leading roomid as column zero and then consumes
     * the three strings in roomlist.  Keep four headers in exactly that
     * order; ASCII placeholders leak straight into the native UI. */
    static const char *const columns[] = {
        "\xb1\xe0\xba\xc5", /* 编号 */
        "\xc0\xde\xcc\xa8", /* 擂台 */
        "\xc0\xde\xd6\xf7", /* 擂主 */
        "\xd7\xb4\xcc\xac"  /* 状态 */
    };

    if (!vm_net_mock_seq_put_string_list(colNames, sizeof(colNames),
                                         &colNamesLen, columns, 4))
    {
        return false;
    }
    vm_net_mock_arena_prune_rooms();
    for (u32 i = 0; i < VM_NET_MOCK_ARENA_ROOM_MAX &&
                    roomCount < VM_NET_MOCK_ARENA_ROOM_PAGE_SIZE;
         ++i)
    {
        const vm_net_mock_arena_room *room = &g_vm_net_mock_arena_rooms[i];
        char title[48];
        const char *state = NULL;

        if (!room->active || room->memberCount == 0)
            continue;
        snprintf(title, sizeof(title), "\xc0\xde\xcc\xa8#%u", room->roomId); /* 擂台 */
        state = room->activeDuelSerial != 0
                    ? "\xb6\xd4\xd5\xbd\xd6\xd0" /* 对战中 */
                    : room->memberCount >= VM_NET_MOCK_ARENA_ROOM_MEMBER_MAX
                    ? "\xd2\xd1\xc2\xfa" /* 已满 */
                    : "\xb5\xc8\xb4\xfd\xcc\xf4\xd5\xbd"; /* 等待挑战 */
        if (!vm_net_mock_seq_put_u32(roomList, sizeof(roomList), &roomListLen,
                                     room->roomId) ||
            !vm_net_mock_seq_put_string(roomList, sizeof(roomList), &roomListLen,
                                        title) ||
            !vm_net_mock_seq_put_string(roomList, sizeof(roomList), &roomListLen,
                                        room->members[0].name) ||
            !vm_net_mock_seq_put_string(roomList, sizeof(roomList), &roomListLen,
                                        state))
        {
            return false;
        }
        ++roomCount;
    }
    if (!vm_net_mock_begin_wt_object(out, outCap, pos, 1, 30, 3,
                                     &objectStart) ||
        !vm_net_mock_put_object_u8(out, outCap, pos, "curpage", 1) ||
        !vm_net_mock_put_object_u8(out, outCap, pos, "pagenum", 1) ||
        !vm_net_mock_put_object_u8(out, outCap, pos, "colnum", 4) ||
        !vm_net_mock_put_object_raw(out, outCap, pos, "colnames", colNames,
                                    (u16)colNamesLen) ||
        !vm_net_mock_put_object_u8(out, outCap, pos, "roomnum", roomCount) ||
        !vm_net_mock_put_object_raw(out, outCap, pos, "roomlist", roomList,
                                    (u16)roomListLen))
    {
        return false;
    }
    vm_net_mock_finish_wt_object(out, objectStart, *pos);
    return true;
}

static const char *vm_net_mock_arena_job_name(u8 job)
{
    switch (job)
    {
    case 2:
        return "\xb4\xcc\xbf\xcd"; /* 刺客 */
    case 3:
        return "\xb7\xa8\xca\xa6"; /* 法师 */
    default:
        return "\xd5\xbd\xca\xbf"; /* 战士 */
    }
}

static bool vm_net_mock_arena_append_roles_object(u8 *out, u32 outCap,
                                                  u32 *pos,
                                                  const vm_net_mock_arena_room *room,
                                                  u32 responseRoomId)
{
    u8 rolesInfo[512];
    u8 colNames[96];
    u32 rolesInfoLen = 0;
    u32 colNamesLen = 0;
    u8 roleCount = 0;
    u32 objectStart = 0;
    static const char *const columns[] = {
        "\xd0\xd5\xc3\xfb", /* 姓名 */
        "\xd6\xb0\xd2\xb5", /* 职业 */
        "\xb5\xc8\xbc\xb6", /* 等级 */
        "\xd7\xb4\xcc\xac"  /* 状态 */
    };

    if (!vm_net_mock_seq_put_string_list(colNames, sizeof(colNames),
                                         &colNamesLen, columns, 4))
    {
        return false;
    }
    if (room != NULL && room->active)
    {
        for (u8 i = 0; i < room->memberCount; ++i)
        {
            char levelText[16];
            const char *state = i == 0 ? "\xc0\xde\xd6\xf7" /* 擂主 */
                                       : "\xcc\xf4\xd5\xbd\xd5\xdf"; /* 挑战者 */

            snprintf(levelText, sizeof(levelText), "%u\xbc\xb6",
                     room->members[i].level ? room->members[i].level : 1);
            if (!vm_net_mock_seq_put_string(rolesInfo, sizeof(rolesInfo),
                                            &rolesInfoLen,
                                            room->members[i].name) ||
                !vm_net_mock_seq_put_string(rolesInfo, sizeof(rolesInfo),
                                            &rolesInfoLen,
                                            vm_net_mock_arena_job_name(
                                                room->members[i].job)) ||
                !vm_net_mock_seq_put_string(rolesInfo, sizeof(rolesInfo),
                                            &rolesInfoLen, levelText) ||
                !vm_net_mock_seq_put_string(rolesInfo, sizeof(rolesInfo),
                                            &rolesInfoLen, state))
            {
                return false;
            }
            ++roleCount;
        }
    }
    if (!vm_net_mock_begin_wt_object(out, outCap, pos, 1, 30, 7,
                                     &objectStart) ||
        !vm_net_mock_put_object_u32(out, outCap, pos, "roomid",
                                    responseRoomId) ||
        !vm_net_mock_put_object_u8(out, outCap, pos, "colnum", 4) ||
        !vm_net_mock_put_object_raw(out, outCap, pos, "colnames", colNames,
                                    (u16)colNamesLen) ||
        !vm_net_mock_put_object_u8(out, outCap, pos, "rolenum", roleCount) ||
        !vm_net_mock_put_object_raw(out, outCap, pos, "rolesinfo", rolesInfo,
                                    (u16)rolesInfoLen))
    {
        return false;
    }
    vm_net_mock_finish_wt_object(out, objectStart, *pos);
    return true;
}

/* Opening the NPC activity must finish in mode 28.  Sending a 30/3 room list
 * in the same response would run after 30/4 and overwrite that creation mode
 * with mode 27, making the native 30/11 creation action unreachable. */
static bool vm_net_mock_arena_append_open_create_form_objects(u8 *out,
                                                              u32 outCap,
                                                              u32 *pos,
                                                              u8 *count)
{
    if (!vm_net_mock_arena_append_open_result_object(out, outCap, pos) ||
        !vm_net_mock_arena_append_config_object(out, outCap, pos))
    {
        return false;
    }
    *count += 2;
    return true;
}

/* Challenge is deliberately a separate first response from creation.  The
 * client writes one task-hall mode per 30/x object, so composing 30/4 and
 * 30/3 here would make the latter overwrite the former. */
static bool vm_net_mock_arena_append_open_challenge_list_objects(u8 *out,
                                                                 u32 outCap,
                                                                 u32 *pos,
                                                                 u8 *count)
{
    if (!vm_net_mock_arena_append_open_result_object(out, outCap, pos) ||
        !vm_net_mock_arena_append_room_list_object(out, outCap, pos))
    {
        return false;
    }
    *count += 2;
    return true;
}

/* 2/13 is the parser-confirmed independent role-energy update.  It does not
 * replace the task-hall mode selected by the preceding 30/3 or 30/4 object. */
static bool vm_net_mock_arena_append_vitality_update_object(u8 *out,
                                                            u32 outCap,
                                                            u32 *pos,
                                                            u32 vitality,
                                                            u32 vitalityMax)
{
    u32 objectStart = 0;

    if (!vm_net_mock_begin_wt_object(out, outCap, pos, 1, 2, 13,
                                     &objectStart) ||
        !vm_net_mock_put_object_u32(out, outCap, pos, "energy", vitality) ||
        !vm_net_mock_put_object_u32(out, outCap, pos, "energymax",
                                    vitalityMax))
    {
        return false;
    }
    vm_net_mock_finish_wt_object(out, objectStart, *pos);
    return true;
}

static bool vm_net_mock_arena_award_step_is_valid(u32 award)
{
    const u32 steps[] = {
        VM_NET_MOCK_ARENA_AWARD_STEP_LOW,
        VM_NET_MOCK_ARENA_AWARD_STEP_MID,
        VM_NET_MOCK_ARENA_AWARD_STEP_HIGH
    };

    if (award == 0 || award > VM_NET_MOCK_ARENA_MAX_AWARD)
        return false;
    for (u32 i = 0; i < sizeof(steps) / sizeof(steps[0]); ++i)
    {
        if (award >= steps[i] && award % steps[i] == 0)
            return true;
    }
    return false;
}

static bool vm_net_mock_arena_create_room(vm_net_mock_role_state *role,
                                          const vm_mock_service_client_session *session,
                                          u8 type, u32 award, u8 turns,
                                          u32 *roomIdOut, u32 *vitalityOut,
                                          u32 *vitalityMaxOut,
                                          const char **reasonOut)
{
    vm_net_mock_arena_room *room = NULL;
    vm_mock_service_online_session_view sessionView;
    u32 vitalityCost = 0;

    if (roomIdOut)
        *roomIdOut = 0;
    if (vitalityOut)
        *vitalityOut = 0;
    if (vitalityMaxOut)
        *vitalityMaxOut = 0;
    if (reasonOut)
        *reasonOut = "invalid-request";
    if (role == NULL || session == NULL || role->roleId == 0 ||
        type > 1 || turns == 0 || turns > VM_NET_MOCK_ARENA_MAX_TURNS ||
        !vm_net_mock_arena_award_step_is_valid(award) ||
        !vm_mock_service_session_get_online_view(session, &sessionView))
    {
        return false;
    }
    vitalityCost = (u32)turns * VM_NET_MOCK_ARENA_VITALITY_PER_TURN;
    vm_net_mock_arena_prune_rooms();
    for (u32 i = 0; i < VM_NET_MOCK_ARENA_ROOM_MAX; ++i)
    {
        if (g_vm_net_mock_arena_rooms[i].active &&
            g_vm_net_mock_arena_rooms[i].members[0].roleId == role->roleId)
        {
            if (roomIdOut)
                *roomIdOut = g_vm_net_mock_arena_rooms[i].roomId;
            if (!vm_net_mock_vitality_snapshot(role, vitalityOut,
                                               vitalityMaxOut))
            {
                if (reasonOut)
                    *reasonOut = "vitality-snapshot-failed";
                return false;
            }
            if (reasonOut)
                *reasonOut = "already-host";
            return true;
        }
    }
    room = vm_net_mock_arena_allocate_room();
    if (room == NULL)
    {
        if (reasonOut)
            *reasonOut = "room-capacity";
        return false;
    }
    if (!vm_net_mock_vitality_consume(role, vitalityCost, vitalityOut,
                                      vitalityMaxOut))
    {
        if (reasonOut)
            *reasonOut = "insufficient-vitality";
        return false;
    }
    memset(room, 0, sizeof(*room));
    room->active = true;
    room->roomId = g_vm_net_mock_arena_next_room_id++;
    if (g_vm_net_mock_arena_next_room_id == 0)
        g_vm_net_mock_arena_next_room_id = 1;
    room->type = type;
    room->award = (u16)award;
    room->turns = turns;
    room->memberCount = 1;
    room->createdTick = g_schedulerTick;
    room->members[0].roleId = role->roleId;
    room->members[0].job = sessionView.onlineJob;
    room->members[0].level = sessionView.onlineLevel;
    snprintf(room->members[0].name, sizeof(room->members[0].name), "%s",
             sessionView.onlineRoleName[0] ? sessionView.onlineRoleName : role->name);
    if (roomIdOut)
        *roomIdOut = room->roomId;
    if (reasonOut)
        *reasonOut = "created";
    return true;
}

static bool vm_net_mock_arena_join_room(vm_net_mock_arena_room *room,
                                        const vm_net_mock_role_state *role,
                                        const vm_mock_service_client_session *session)
{
    vm_mock_service_online_session_view sessionView;

    if (room == NULL || !room->active || role == NULL || session == NULL ||
        !vm_mock_service_session_get_online_view(session, &sessionView))
        return false;
    for (u8 i = 0; i < room->memberCount; ++i)
    {
        if (room->members[i].roleId == role->roleId)
            return true;
    }
    if (room->memberCount >= VM_NET_MOCK_ARENA_ROOM_MEMBER_MAX)
        return false;
    room->members[room->memberCount].roleId = role->roleId;
    room->members[room->memberCount].job = sessionView.onlineJob;
    room->members[room->memberCount].level = sessionView.onlineLevel;
    snprintf(room->members[room->memberCount].name,
             sizeof(room->members[room->memberCount].name), "%s",
             sessionView.onlineRoleName[0] ? sessionView.onlineRoleName : role->name);
    ++room->memberCount;
    return true;
}

/* Returns non-zero only for the precise, context-authorised arena traffic.
 * Generic scene channel packets remain available to their existing handlers. */
u32 vm_net_mock_build_arena_response(const u8 *request, u32 requestLen,
                                     u8 *out, u32 outCap)
{
    vm_net_mock_role_state *role = vm_net_mock_active_role();
    vm_mock_service_client_session *session =
        vm_mock_service_get_active_client_session();
    vm_mock_service_online_session_view sessionView;
    u32 serviceValue = 0;
    vm_net_mock_request_object object;
    u32 roomId = 0;
    u32 award = 0;
    u32 vitality = 0;
    u32 vitalityMax = 0;
    u8 type = 0;
    u8 turns = 0;
    u8 agree = 0;
    u32 pos = 5;
    u8 objectCount = 0;
    const char *action = NULL;
    const char *createReason = NULL;

    if (role == NULL || session == NULL || out == NULL || outCap < pos ||
        !vm_mock_service_session_get_online_view(session, &sessionView))
        return 0;
    if (vm_net_mock_is_npc_service_dialog_request(request, requestLen,
                                                   &serviceValue))
    {
        if (!vm_net_mock_arena_context_is_active(role, session))
        {
            return 0;
        }
        if (serviceValue == VM_NET_MOCK_NPC_SERVICE_OPEN_ARENA_CREATE)
        {
            if (!vm_net_mock_arena_append_open_create_form_objects(
                    out, outCap, &pos, &objectCount))
            {
                return 0;
            }
            action = "open-create-form";
        }
        else if (serviceValue == VM_NET_MOCK_NPC_SERVICE_OPEN_ARENA_CHALLENGE)
        {
            if (!vm_net_mock_arena_append_open_challenge_list_objects(
                    out, outCap, &pos, &objectCount))
            {
                return 0;
            }
            action = "open-challenge-list";
        }
        else
        {
            return 0;
        }
    }
    else if (vm_net_mock_arena_request_object(request, requestLen, 3,
                                               &object))
    {
        /* The list screen uses 30/3{page} for its arrows.  The current
         * activity has one bounded page, but it must still answer the native
         * page request rather than leaving the task-hall wait indicator up. */
        if (!vm_net_mock_arena_context_is_active(role, session) ||
            !vm_net_mock_arena_append_room_list_object(out, outCap, &pos))
        {
            return 0;
        }
        objectCount = 1;
        action = "refresh-room-list";
    }
    else if (vm_net_mock_arena_request_object(request, requestLen, 4,
                                               &object))
    {
        /* A local "开设擂台" action may ask only for the retained parameter
         * block.  30/4 is the sole parser-confirmed response that selects the
         * native creation form, so do not substitute a dialog or a generic
         * success object here. */
        if (!vm_net_mock_arena_context_is_active(role, session) ||
            !vm_net_mock_arena_append_config_object(out, outCap, &pos))
        {
            return 0;
        }
        objectCount = 1;
        action = "open-create-form";
    }
    else if (vm_net_mock_arena_request_object(request, requestLen, 11,
                                               &object))
    {
        u32 createdRoomId = 0;
        bool created = false;

        if (!vm_net_mock_arena_context_is_active(role, session) ||
            !vm_net_mock_get_object_u8_field(object.payload, object.payloadLen,
                                             "type", &type) ||
            !vm_net_mock_get_object_number_field(object.payload,
                                                 object.payloadLen, "award",
                                                 &award) ||
            !vm_net_mock_get_object_u8_field(object.payload, object.payloadLen,
                                             "turns", &turns))
        {
            return 0;
        }
        created = vm_net_mock_arena_create_room(role, session, type, award,
                                                turns, &createdRoomId,
                                                &vitality, &vitalityMax,
                                                &createReason);
        if (!created)
        {
            /* Rejecting a creation must leave the native parameter form open.
             * Returning 30/3 here previously hid the reason behind an empty
             * list and required reopening the NPC to correct the values. */
            if (!vm_net_mock_arena_append_config_object(out, outCap, &pos))
                return 0;
            objectCount = 1;
            action = "create-room-rejected";
            roomId = 0;
        }
        else if (!vm_net_mock_arena_append_room_list_object(out, outCap, &pos) ||
                 !vm_net_mock_arena_append_vitality_update_object(
                     out, outCap, &pos, vitality, vitalityMax))
        {
            return 0;
        }
        else
        {
            /* Creation completes back on the confirmed room-list state. The
             * final 2/13 only updates energy and does not select a hall mode. */
            objectCount = 2;
            action = "create-room";
            roomId = createdRoomId;
        }
    }
    else if (vm_net_mock_arena_request_object(request, requestLen, 7,
                                               &object))
    {
        vm_net_mock_arena_room *room = NULL;

        if (!vm_net_mock_arena_context_is_active(role, session) ||
            !vm_net_mock_get_object_u32_field(object.payload, object.payloadLen,
                                              "roomid", &roomId))
        {
            return 0;
        }
        vm_net_mock_arena_prune_rooms();
        room = vm_net_mock_arena_find_room(roomId);
        if (!vm_net_mock_arena_append_roles_object(out, outCap, &pos, room,
                                                   roomId))
        {
            return 0;
        }
        objectCount = 1;
        action = room != NULL ? "view-room" : "view-room-missing";
    }
    else if (vm_net_mock_arena_request_object(request, requestLen, 8,
                                               &object))
    {
        vm_net_mock_arena_room *room = NULL;
        int memberIndex = -1;
        bool joined = false;

        if (!vm_net_mock_arena_context_is_active(role, session) ||
            !vm_net_mock_get_object_u32_field(object.payload, object.payloadLen,
                                              "roomid", &roomId))
        {
            return 0;
        }
        vm_net_mock_arena_prune_rooms();
        room = vm_net_mock_arena_find_room(roomId);
        memberIndex = vm_net_mock_arena_member_index(room, role->roleId);
        if (memberIndex < 0)
        {
            joined = vm_net_mock_arena_join_room(room, role, session);
            /* First 30/8 is a room join.  The only evidence-backed response
             * here is the room-member list, which returns the client to mode
             * 31 and exposes its native confirm action. */
            if (!vm_net_mock_arena_append_roles_object(out, outCap, &pos,
                                                       room, roomId))
            {
                return 0;
            }
            objectCount = 1;
            action = joined ? "join-room" : "join-room-rejected";
        }
        else if (room == NULL || room->memberCount !=
                                      VM_NET_MOCK_ARENA_ROOM_MEMBER_MAX ||
                 room->activeDuelSerial != 0 || room->challengePending ||
                 room->completedRounds >= room->turns)
        {
            /* No protocol-defined error popup accompanies 30/8.  Keep the
             * user on the authoritative member list instead of emitting an
             * invented failure subtype or starting an incomplete room. */
            if (!vm_net_mock_arena_append_roles_object(out, outCap, &pos,
                                                       room, roomId))
            {
                return 0;
            }
            objectCount = 1;
            action = "challenge-unavailable";
        }
        else
        {
            /* The second native 30/8 in mode 31 is the challenge edge, not a
             * duplicate join.  It must first return to scene.  Delivering
             * 30/9 here leaves its confirmation callback owned by task-hall,
             * so even a later 4/10 has no loaded battle module to consume it. */
            if (!vm_net_mock_arena_append_prepare_scene_confirm_objects(
                    out, outCap, &pos))
            {
                return 0;
            }
            room->challengePending = true;
            room->challengeRoleId = role->roleId;
            room->challengeOpponentRoleId = 0;
            room->challengeTick = g_schedulerTick;
            vm_mock_service_session_set_arena_challenge_state(
                session, true, false, 0);
            objectCount = 2;
            action = "challenge-prepare-scene-prompt";
        }
    }
    else if (vm_net_mock_arena_is_confirm_request(request, requestLen, &agree))
    {
        vm_net_mock_arena_room *room = NULL;
        vm_mock_service_duel *duel = NULL;
        bool opponentPromptQueued = false;
        bool isInitiator = false;

        /* 30/10 is intentionally accepted only while this role is either the
         * recorded challenger or the recorded remote opponent of one live
         * arena challenge.  It is a common scene-channel opcode, so widening
         * this branch would steal unrelated NPC confirmations. */
        vm_net_mock_arena_prune_rooms();
        for (u32 i = 0; i < VM_NET_MOCK_ARENA_ROOM_MAX; ++i)
        {
            vm_net_mock_arena_room *candidate = &g_vm_net_mock_arena_rooms[i];

            if (candidate->active && candidate->challengePending &&
                candidate->challengeRoleId == role->roleId &&
                candidate->challengeOpponentRoleId == 0 &&
                sessionView.arenaChallengeReplyActive &&
                sessionView.arenaChallengeSourceRoleId == role->roleId)
            {
                room = candidate;
                isInitiator = true;
                break;
            }
            if (candidate->active && candidate->challengePending &&
                candidate->challengeRoleId != role->roleId &&
                candidate->challengeOpponentRoleId == role->roleId &&
                sessionView.arenaChallengeReplyActive &&
                sessionView.arenaChallengeSourceRoleId == candidate->challengeRoleId)
            {
                room = candidate;
                break;
            }
        }
        if (room == NULL)
            return 0;
        roomId = room->roomId;
        if (isInitiator)
        {
            /* This 30/10 now belongs to the scene-owned prompt, so result=0
             * clears only its request layer.  The original task-hall was
             * already closed before this prompt was emitted. */
            if (!vm_net_mock_arena_append_confirm_pending_ack_object(
                    out, outCap, &pos))
                return 0;
            objectCount = 1;
            vm_mock_service_session_set_arena_challenge_state(
                session, sessionView.arenaChallengeInitiatorPromptPending,
                false, 0);
            if (agree != 0)
            {
                vm_net_mock_arena_clear_challenge(room);
                action = "challenge-cancelled";
            }
            else
            {
                opponentPromptQueued = vm_net_mock_arena_queue_opponent_challenge(
                    room, session, role, vm_mock_service_active_account_id());
                if (opponentPromptQueued)
                {
                    int challengerIndex = vm_net_mock_arena_member_index(
                        room, role->roleId);
                    room->challengeOpponentRoleId =
                        room->members[1 - challengerIndex].roleId;
                    room->challengeTick = g_schedulerTick;
                    action = "challenge-confirmed-await-opponent";
                }
                else
                {
                    vm_net_mock_arena_clear_challenge(room);
                    action = "challenge-confirmed-opponent-unavailable";
                }
            }
        }
        else
        {
            /* The remote 30/9 was delivered in a scene poll rather than from
             * task hall, so result=0 only clears its request overlay.  On an
             * accept this exact lease, not any generic spar reply, owns duel
             * creation and the following pending 4/10 start delivery. */
            if (!vm_net_mock_arena_append_confirm_pending_ack_object(
                    out, outCap, &pos))
            {
                return 0;
            }
            objectCount = 1;
            vm_mock_service_session_set_arena_challenge_state(
                session, sessionView.arenaChallengeInitiatorPromptPending,
                false, 0);
            if (agree != 0)
            {
                vm_net_mock_arena_clear_challenge(room);
                action = "opponent-rejected";
            }
            else
            {
                duel = vm_net_mock_arena_begin_duel(room,
                                                     room->challengeRoleId);
                if (duel != NULL)
                {
                    vm_net_mock_arena_clear_challenge(room);
                    action = "opponent-accepted-duel-pending";
                }
                else
                {
                    vm_net_mock_arena_clear_challenge(room);
                    action = "opponent-accepted-duel-unavailable";
                }
            }
        }
    }
    else
    {
        return 0;
    }

    vm_net_mock_finish_wt_packet(out, pos, objectCount);
    printf("[info][network] mock_arena action=%s role=%u room=%u type=%u agree=%u award=%u turns=%u vitality=%u/%u reason=%s rooms=%u resp=%u evidence=JianghuOL.CBE:0x01037C02+0x0103965A+0x01049878+0x01049764\n",
           action ? action : "-", role->roleId, roomId, type, agree, award, turns,
           vitality, vitalityMax, createReason ? createReason : "-",
           VM_NET_MOCK_ARENA_ROOM_MAX, pos);
    return pos;
}
