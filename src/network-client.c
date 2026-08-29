/*
 * Remote game-service transport used by client-only builds.
 *
 * This file deliberately contains no request handlers, database access,
 * listener sockets or HTTP administration code.  It only forwards guest WT
 * packets to the configured game server and queues the returned bytes back to
 * the emulated client.
 */

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
typedef SOCKET vm_client_socket;
#define VM_CLIENT_INVALID_SOCKET INVALID_SOCKET
#else
#include <arpa/inet.h>
#include <netdb.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>
typedef int vm_client_socket;
#define VM_CLIENT_INVALID_SOCKET (-1)
#endif

enum
{
    VM_CLIENT_FRAME_SIZE = 20,
    VM_CLIENT_REQUEST_FLAG_SCENE_SYNC_POLL = 0x2u,
    VM_CLIENT_REQUEST_FLAG_DISCONNECT = 0x4u,
    VM_CLIENT_RESPONSE_FLAG_CLOSE_AFTER_DATA = 0x1u,
    VM_CLIENT_SOCKET_TIMEOUT_MS = 5000,
    VM_CLIENT_REQUEST_MAX = 512,
    VM_CLIENT_QUEUE_MAX = 64,
    VM_CLIENT_FOLLOWUP_MAX = 65536
};

/* These emulator helpers historically lived in mock-server.c because that
 * file was included into main.c.  Client-only builds still need them for
 * resource lookup, billing-module names and executable pool diagnostics. */
static bool vm_host_file_exists(const char *path)
{
    FILE *fp = fopen(path, "rb");
    if (fp == NULL)
        return false;
    fclose(fp);
    return true;
}

static u32 vm_alloc_host_string(const char *text)
{
    u32 len;
    u32 ptr;
    if (text == NULL)
        return 0;
    len = (u32)strlen(text) + 1;
    ptr = vm_malloc(len);
    if (ptr != 0)
        uc_mem_write(MTK, ptr, text, len);
    return ptr;
}

static u32 vm_net_mock_sync_buffer_to_vm(const u8 *buffer, u32 bufferLen)
{
    u32 responsePtr;
    if (buffer == NULL || bufferLen == 0)
        return 0;
    responsePtr = vm_malloc(bufferLen);
    if (responsePtr == 0)
        return 0;
    if (uc_mem_write(MTK, responsePtr, buffer, bufferLen) != UC_ERR_OK)
        return 0;
    return responsePtr;
}

typedef struct
{
    u8 major;
    u8 kind;
    u8 subtype;
    const u8 *payload;
    u16 payloadLen;
} vm_client_wt_object;

static bool vm_client_next_wt_object(const u8 *packet, u32 packetLen,
                                     u32 *offset, vm_client_wt_object *object)
{
    u32 start;
    u16 objectLen;
    if (packet == NULL || offset == NULL || *offset < 5 || *offset + 6 > packetLen)
        return false;
    start = *offset;
    objectLen = (u16)(((u16)packet[start + 4] << 8) | packet[start + 5]);
    if (objectLen < 6 || start + objectLen > packetLen)
        return false;
    if (object != NULL)
    {
        object->major = packet[start];
        object->kind = packet[start + 1];
        object->subtype = packet[start + 2];
        object->payload = packet + start + 6;
        object->payloadLen = (u16)(objectLen - 6);
    }
    *offset = start + objectLen;
    return true;
}

static void vm_client_finish_wt_packet(u8 *packet, u32 len, u8 objectCount)
{
    packet[0] = 'W';
    packet[1] = 'T';
    packet[2] = (u8)(len >> 8);
    packet[3] = (u8)len;
    packet[4] = objectCount;
}

typedef enum
{
    VM_CLIENT_ITEM_FOLLOWUP_NONE = 0,
    VM_CLIENT_ITEM_FOLLOWUP_USE_LIST
} vm_client_item_followup_kind;

static bool vm_client_extract_item_followup(u8 *response, u32 *responseLen,
                                            u8 *followup, u32 followupCap,
                                            u32 *followupLen,
                                            vm_client_item_followup_kind *kindOut)
{
    u32 offset = 5;
    u32 primaryPos = 5;
    u32 followPos = 5;
    u8 primaryCount = 0;
    u8 followCount = 0;
    u8 seenCount = 0;
    bool haveItemUse = false;
    bool haveSilentCompletion = false;
    u8 itemListCount = 0;
    u8 itemGridCount = 0;
    vm_client_item_followup_kind followupKind =
        VM_CLIENT_ITEM_FOLLOWUP_NONE;
    vm_client_wt_object object;

    if (followupLen != NULL)
        *followupLen = 0;
    if (kindOut != NULL)
        *kindOut = VM_CLIENT_ITEM_FOLLOWUP_NONE;
    if (response == NULL || responseLen == NULL || *responseLen < 10 ||
        response[0] != 'W' || response[1] != 'T' ||
        followup == NULL || followupCap < 5)
        return false;

    while (offset + 5 <= *responseLen &&
           vm_client_next_wt_object(response, *responseLen, &offset, &object))
    {
        /* The legacy acknowledgement is 7/1, while the silent completion
         * contract used by ordinary recovery items is 7/4.  Both must be
         * delivered before the full 17/1 list: the CBE operation handler
         * clears its pending-use state in that acknowledgement branch, and
         * the backpack screen owns the list replacement callback. */
        if (object.major == 1 && object.kind == 7 &&
            (object.subtype == 1 || object.subtype == 4))
        {
            haveItemUse = true;
            if (object.subtype == 4)
                haveSilentCompletion = true;
        }
        if (object.major == 1 && object.kind == 17 && object.subtype == 1)
            ++itemListCount;
        if (object.major == 1 && object.kind == 30 && object.subtype == 21)
            ++itemGridCount;
        ++seenCount;
    }
    if (offset != *responseLen || seenCount != response[4])
        return false;

    /* Existing item-use flow: the business acknowledgement must complete
     * before its full backpack-list follow-up reaches the list owner.  The
     * silent 7/4 completion is a stronger boundary: unlike the legacy 7/1
     * response, every object after it belongs to the same refresh transaction
     * and must retain its wire order (30/21 -> 7/42 -> 7/11 -> 7/37). */
    if (haveItemUse && (itemListCount != 0 || itemGridCount != 0))
    {
        followupKind = VM_CLIENT_ITEM_FOLLOWUP_USE_LIST;
    }
    else
    {
        return false;
    }

    offset = 5;
    primaryPos = 5;
    followPos = 5;
    primaryCount = 0;
    followCount = 0;
    while (offset + 5 <= *responseLen)
    {
        u32 start = offset;
        u32 objectLen;
        if (!vm_client_next_wt_object(response, *responseLen, &offset, &object))
            return false;
        objectLen = offset - start;
        bool isCompletion = object.major == 1 && object.kind == 7 &&
                            object.subtype == 4;
        bool isFollowup = false;

        if (followupKind == VM_CLIENT_ITEM_FOLLOWUP_USE_LIST)
        {
            if (haveSilentCompletion)
            {
                /* Remote transport delivers the primary frame first.  Keep
                 * only the silent completion in that frame; moving the
                 * selected-row 7/11 ahead of 30/21 was the source of the
                 * stale/single-stack quantity (20 -> 9) seen remotely. */
                isFollowup = !isCompletion;
            }
            else
            {
                isFollowup =
                    (object.major == 1 && object.kind == 17 &&
                     object.subtype == 1) ||
                    (object.major == 1 && object.kind == 30 &&
                     object.subtype == 21);
            }
        }
        if (isFollowup)
        {
            if (followPos + objectLen > followupCap || followCount == 0xff)
                return false;
            memcpy(followup + followPos, response + start, objectLen);
            followPos += objectLen;
            ++followCount;
        }
        else
        {
            memmove(response + primaryPos, response + start, objectLen);
            primaryPos += objectLen;
            ++primaryCount;
        }
    }
    vm_client_finish_wt_packet(response, primaryPos, primaryCount);
    vm_client_finish_wt_packet(followup, followPos, followCount);
    *responseLen = primaryPos;
    if (followupLen != NULL)
        *followupLen = followPos;
    if (kindOut != NULL)
        *kindOut = followupKind;
    return true;
}

/* SendNPCInteractReq(action13) uses the task-hall acknowledgement as a
 * complete UI transaction.  DispatchItemEvent clears that transaction for
 * 26/0, but a scene battle must enter through the following normal data event.
 * Keep the server's bytes intact while delivering the two parser-backed
 * transactions through the existing event-7 queue. */
static bool vm_client_extract_action13_battle_followup(
    u8 *response, u32 *responseLen, u8 *followup, u32 followupCap,
    u32 *followupLen)
{
    u32 starts[3];
    u32 lengths[3];
    u32 offset = 5;
    u32 objectCount = 0;
    u32 primaryLen = 5;
    u32 followLen = 5;
    vm_client_wt_object objects[3];

    if (followupLen != NULL)
        *followupLen = 0;
    if (response == NULL || responseLen == NULL || *responseLen < 23 ||
        response[0] != 'W' || response[1] != 'T' || response[4] != 3 ||
        followup == NULL || followupCap < 5)
    {
        return false;
    }
    while (offset < *responseLen && objectCount < 3)
    {
        starts[objectCount] = offset;
        if (!vm_client_next_wt_object(response, *responseLen, &offset,
                                      &objects[objectCount]))
        {
            return false;
        }
        lengths[objectCount] = offset - starts[objectCount];
        ++objectCount;
    }
    if (offset != *responseLen || objectCount != 3 ||
        objects[0].major != 1 || objects[0].kind != 26 ||
        objects[0].subtype != 0 || objects[0].payloadLen != 0 ||
        objects[1].major != 1 || objects[1].kind != 2 ||
        objects[1].subtype != 2 || objects[2].major != 1 ||
        objects[2].kind != 4 || objects[2].subtype != 5)
    {
        return false;
    }

    primaryLen += lengths[0];
    followLen += lengths[1] + lengths[2];
    if (followLen > followupCap)
        return false;
    memmove(response + 5, response + starts[0], lengths[0]);
    memcpy(followup + 5, response + starts[1], lengths[1]);
    memcpy(followup + 5 + lengths[1], response + starts[2], lengths[2]);
    vm_client_finish_wt_packet(response, primaryLen, 1);
    vm_client_finish_wt_packet(followup, followLen, 2);
    *responseLen = primaryLen;
    if (followupLen != NULL)
        *followupLen = followLen;
    printf("[info][network] remote_action13_challenge_split primary=26/0 "
           "followup=2/2+4/5 delivery=event7-then-event7 "
           "evidence=JianghuOL.CBE:0x01039C28+mmBattle:0x66CC\n");
    return true;
}

static void vm_client_write_le32(u8 *dst, u32 value)
{
    dst[0] = (u8)value;
    dst[1] = (u8)(value >> 8);
    dst[2] = (u8)(value >> 16);
    dst[3] = (u8)(value >> 24);
}

static u32 vm_client_read_le32(const u8 *src)
{
    return (u32)src[0] | ((u32)src[1] << 8) |
           ((u32)src[2] << 16) | ((u32)src[3] << 24);
}

static void vm_client_encode_header(u8 *header, u32 flags, u32 bodyLen, u32 metaLen)
{
    memcpy(header, "CBMS", 4);
    vm_client_write_le32(header + 4, 1);
    vm_client_write_le32(header + 8, flags);
    vm_client_write_le32(header + 12, bodyLen);
    vm_client_write_le32(header + 16, metaLen);
}

static bool vm_client_socket_init(void)
{
#ifdef _WIN32
    static int initialized = -1;
    WSADATA wsaData;

    if (initialized >= 0)
        return initialized != 0;
    initialized = WSAStartup(MAKEWORD(2, 2), &wsaData) == 0 ? 1 : 0;
    return initialized != 0;
#else
    return true;
#endif
}

static void vm_client_close_socket(vm_client_socket sock)
{
    if (sock != VM_CLIENT_INVALID_SOCKET)
#ifdef _WIN32
        closesocket(sock);
#else
        close(sock);
#endif
}

static bool vm_client_send_all(vm_client_socket sock, const u8 *data, u32 len)
{
    u32 sent = 0;
    while (sent < len)
    {
        int rc = send(sock, (const char *)(data + sent), (int)(len - sent), 0);
        if (rc <= 0)
            return false;
        sent += (u32)rc;
    }
    return true;
}

static bool vm_client_recv_all(vm_client_socket sock, u8 *data, u32 len)
{
    u32 received = 0;
    while (received < len)
    {
        int rc = recv(sock, (char *)(data + received), (int)(len - received), 0);
        if (rc <= 0)
            return false;
        received += (u32)rc;
    }
    return true;
}

static vm_client_socket vm_client_connect(void)
{
    struct addrinfo hints;
    struct addrinfo *addresses = NULL;
    struct addrinfo *address;
#ifndef _WIN32
    struct timeval timeout;
#endif
    char port[16];
    vm_client_socket sock = VM_CLIENT_INVALID_SOCKET;

    if (!vm_client_socket_init())
        return VM_CLIENT_INVALID_SOCKET;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    snprintf(port, sizeof(port), "%u", g_mockServicePort);
    if (getaddrinfo(g_mockServiceHost, port, &hints, &addresses) != 0)
        return VM_CLIENT_INVALID_SOCKET;

#ifdef _WIN32
    {
        int timeout = VM_CLIENT_SOCKET_TIMEOUT_MS;
#else
    timeout.tv_sec = VM_CLIENT_SOCKET_TIMEOUT_MS / 1000;
    timeout.tv_usec = (VM_CLIENT_SOCKET_TIMEOUT_MS % 1000) * 1000;
#endif
    for (address = addresses; address != NULL; address = address->ai_next)
    {
        sock = socket(address->ai_family, address->ai_socktype, address->ai_protocol);
        if (sock == VM_CLIENT_INVALID_SOCKET)
            continue;
#ifdef _WIN32
        setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, (const char *)&timeout, sizeof(timeout));
        setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, (const char *)&timeout, sizeof(timeout));
#else
        setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
        setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));
#endif
        if (connect(sock, address->ai_addr, address->ai_addrlen) == 0)
            break;
        vm_client_close_socket(sock);
        sock = VM_CLIENT_INVALID_SOCKET;
    }
#ifdef _WIN32
    }
#endif
    freeaddrinfo(addresses);
    return sock;
}

static u32 vm_client_encode_meta(u8 *meta, u32 cap)
{
    if (meta == NULL || cap < 4 || g_mockServiceClientId == 0)
        return 0;
    vm_client_write_le32(meta, g_mockServiceClientId);
    return 4;
}

static bool vm_client_read_response(vm_client_socket sock, u8 *response, u32 responseCap,
                                    u32 *responseLen, u32 *eventType,
                                    bool *closeAfterData)
{
    u8 header[VM_CLIENT_FRAME_SIZE];
    u32 flags;
    u32 len;
    u32 event;
    if (!vm_client_recv_all(sock, header, sizeof(header)) ||
        memcmp(header, "CBMR", 4) != 0 || vm_client_read_le32(header + 4) != 1)
        return false;
    flags = vm_client_read_le32(header + 8);
    len = vm_client_read_le32(header + 12);
    event = vm_client_read_le32(header + 16);
    if (len > responseCap || (len != 0 && !vm_client_recv_all(sock, response, len)))
        return false;
    if (responseLen != NULL)
        *responseLen = len;
    if (eventType != NULL)
        *eventType = event == 0 ? 7 : event;
    if (closeAfterData != NULL)
        *closeAfterData = (flags & VM_CLIENT_RESPONSE_FLAG_CLOSE_AFTER_DATA) != 0;
    return true;
}

static bool vm_client_remote_request(const u8 *request, u32 requestLen,
                                     u8 *response, u32 responseCap,
                                     u32 *responseLen, u32 *eventType,
                                     bool *closeAfterData,
                                     u8 *followup, u32 followupCap,
                                     u32 *followupLen,
                                     bool *followupNextSchedulerTick)
{
    u8 header[VM_CLIENT_FRAME_SIZE];
    u8 meta[16];
    u32 metaLen = vm_client_encode_meta(meta, sizeof(meta));
    vm_client_socket sock;
    bool ok;

    if (responseLen != NULL)
        *responseLen = 0;
    if (eventType != NULL)
        *eventType = 7;
    if (closeAfterData != NULL)
        *closeAfterData = false;
    if (followupLen != NULL)
        *followupLen = 0;
    if (followupNextSchedulerTick != NULL)
        *followupNextSchedulerTick = false;
    if (request == NULL || requestLen == 0 || response == NULL || metaLen == 0)
        return false;

    sock = vm_client_connect();
    if (sock == VM_CLIENT_INVALID_SOCKET)
        return false;
    vm_client_encode_header(header, 0, requestLen + metaLen, metaLen);
    ok = vm_client_send_all(sock, header, sizeof(header)) &&
         vm_client_send_all(sock, meta, metaLen) &&
         vm_client_send_all(sock, request, requestLen) &&
         vm_client_read_response(sock, response, responseCap,
                                 responseLen, eventType, closeAfterData);
    vm_client_close_socket(sock);
    if (ok && eventType != NULL && *eventType == 7 && responseLen != NULL &&
        followup != NULL && followupLen != NULL &&
        !vm_client_extract_item_followup(response, responseLen, followup,
                                         followupCap, followupLen, NULL))
    {
        if (vm_client_extract_action13_battle_followup(
                     response, responseLen, followup, followupCap,
                     followupLen) &&
                 followupNextSchedulerTick != NULL)
        {
            *followupNextSchedulerTick = true;
        }
    }
    return ok;
}

static bool vm_client_remote_poll(u8 *response, u32 responseCap,
                                  u32 *responseLen, u32 *eventType)
{
    u8 header[VM_CLIENT_FRAME_SIZE];
    u8 meta[16];
    u32 metaLen = vm_client_encode_meta(meta, sizeof(meta));
    vm_client_socket sock;
    bool ok;
    if (responseLen != NULL)
        *responseLen = 0;
    if (eventType != NULL)
        *eventType = 7;
    if (response == NULL || metaLen == 0)
        return false;
    sock = vm_client_connect();
    if (sock == VM_CLIENT_INVALID_SOCKET)
        return false;
    vm_client_encode_header(header, VM_CLIENT_REQUEST_FLAG_SCENE_SYNC_POLL,
                            metaLen, metaLen);
    ok = vm_client_send_all(sock, header, sizeof(header)) &&
         vm_client_send_all(sock, meta, metaLen) &&
         vm_client_read_response(sock, response, responseCap,
                                 responseLen, eventType, NULL);
    vm_client_close_socket(sock);
    return ok;
}

static void vm_net_mock_service_notify_disconnect(const char *reason)
{
    u8 header[VM_CLIENT_FRAME_SIZE];
    u8 responseHeader[VM_CLIENT_FRAME_SIZE];
    u8 meta[16];
    u32 metaLen;
    vm_client_socket sock;
    bool ok;
    if (g_mockServiceClientId == 0)
        return;
    metaLen = vm_client_encode_meta(meta, sizeof(meta));
    sock = vm_client_connect();
    if (sock == VM_CLIENT_INVALID_SOCKET)
        return;
    vm_client_encode_header(header, VM_CLIENT_REQUEST_FLAG_DISCONNECT,
                            metaLen, metaLen);
    ok = vm_client_send_all(sock, header, sizeof(header)) &&
         vm_client_send_all(sock, meta, metaLen) &&
         vm_client_recv_all(sock, responseHeader, sizeof(responseHeader)) &&
         memcmp(responseHeader, "CBMR", 4) == 0 &&
         vm_client_read_le32(responseHeader + 4) == 1;
    vm_client_close_socket(sock);
    printf("[info][network] disconnect client=%08x result=%s reason=%s\n",
           g_mockServiceClientId, ok ? "ok" : "failed", reason ? reason : "-");
}

typedef enum
{
    VM_CLIENT_JOB_DATA = 1,
    VM_CLIENT_JOB_SCENE_POLL = 2
} vm_client_job_kind;

typedef struct vm_client_job
{
    struct vm_client_job *next;
    u32 generation;
    u32 sequence;
    u32 enqueueMs;
    u32 connectId;
    u32 requestLen;
    vm_client_job_kind kind;
    u8 request[VM_CLIENT_REQUEST_MAX];
} vm_client_job;

typedef struct vm_client_completion
{
    struct vm_client_completion *next;
    u32 generation;
    u32 sequence;
    u32 enqueueMs;
    u32 workerStartMs;
    u32 workerDoneMs;
    u32 connectId;
    u32 eventType;
    u32 responseLen;
    u32 followupLen;
    vm_client_job_kind kind;
    bool success;
    bool closeAfterData;
    bool followupNextSchedulerTick;
    u8 *response;
    u8 *followup;
} vm_client_completion;

typedef struct
{
    pthread_mutex_t mutex;
    pthread_cond_t condition;
    pthread_t worker;
    bool workerStarted;
    bool stopRequested;
    bool scenePollOutstanding;
    u32 generation;
    u32 nextSequence;
    u32 queuedJobs;
    vm_client_job *jobHead;
    vm_client_job *jobTail;
    vm_client_completion *completionHead;
    vm_client_completion *completionTail;
} vm_client_async_state;

static vm_client_async_state g_vmClientAsync = {
    PTHREAD_MUTEX_INITIALIZER, PTHREAD_COND_INITIALIZER, 0,
    false, false, false, 1, 1, 0, NULL, NULL, NULL, NULL};

static void vm_client_free_completion(vm_client_completion *completion)
{
    if (completion == NULL)
        return;
    free(completion->response);
    free(completion->followup);
    free(completion);
}

static void *vm_client_worker_main(void *unused)
{
    u8 *responseScratch = (u8 *)malloc(sizeof(g_netMockResponse));
    u8 *followupScratch = (u8 *)malloc(VM_CLIENT_FOLLOWUP_MAX);
    (void)unused;
    for (;;)
    {
        vm_client_job *job;
        vm_client_completion *completion;
        u32 responseLen = 0;
        u32 eventType = 7;
        u32 followupLen = 0;
        bool closeAfterData = false;
        bool followupNextSchedulerTick = false;
        bool success;

        pthread_mutex_lock(&g_vmClientAsync.mutex);
        while (!g_vmClientAsync.stopRequested && g_vmClientAsync.jobHead == NULL)
            pthread_cond_wait(&g_vmClientAsync.condition, &g_vmClientAsync.mutex);
        if (g_vmClientAsync.stopRequested)
        {
            pthread_mutex_unlock(&g_vmClientAsync.mutex);
            break;
        }
        job = g_vmClientAsync.jobHead;
        g_vmClientAsync.jobHead = job->next;
        if (g_vmClientAsync.jobHead == NULL)
            g_vmClientAsync.jobTail = NULL;
        if (g_vmClientAsync.queuedJobs != 0)
            --g_vmClientAsync.queuedJobs;
        pthread_mutex_unlock(&g_vmClientAsync.mutex);

        completion = (vm_client_completion *)calloc(1, sizeof(*completion));
        if (completion == NULL || responseScratch == NULL || followupScratch == NULL)
        {
            bool wasPoll = job->kind == VM_CLIENT_JOB_SCENE_POLL;
            free(job);
            free(completion);
            if (wasPoll)
            {
                pthread_mutex_lock(&g_vmClientAsync.mutex);
                g_vmClientAsync.scenePollOutstanding = false;
                pthread_mutex_unlock(&g_vmClientAsync.mutex);
            }
            continue;
        }
        completion->generation = job->generation;
        completion->sequence = job->sequence;
        completion->enqueueMs = job->enqueueMs;
        completion->workerStartMs = SDL_GetTicks();
        completion->connectId = job->connectId;
        completion->kind = job->kind;
        completion->eventType = 7;
        if (job->kind == VM_CLIENT_JOB_SCENE_POLL)
        {
            success = vm_client_remote_poll(responseScratch, sizeof(g_netMockResponse),
                                            &responseLen, &eventType);
        }
        else
        {
            success = vm_client_remote_request(job->request, job->requestLen,
                                               responseScratch, sizeof(g_netMockResponse),
                                               &responseLen, &eventType,
                                               &closeAfterData,
                                               followupScratch, VM_CLIENT_FOLLOWUP_MAX,
                                               &followupLen,
                                               &followupNextSchedulerTick);
        }
        completion->workerDoneMs = SDL_GetTicks();
        completion->success = success;
        completion->closeAfterData = closeAfterData;
        completion->eventType = eventType;
        completion->responseLen = responseLen;
        completion->followupLen = followupLen;
        completion->followupNextSchedulerTick = followupNextSchedulerTick;
        if (success && responseLen != 0)
        {
            completion->response = (u8 *)malloc(responseLen);
            if (completion->response != NULL)
                memcpy(completion->response, responseScratch, responseLen);
            else
                completion->success = false;
        }
        if (completion->success && followupLen != 0)
        {
            completion->followup = (u8 *)malloc(followupLen);
            if (completion->followup != NULL)
                memcpy(completion->followup, followupScratch, followupLen);
            else
                completion->success = false;
        }
        free(job);

        pthread_mutex_lock(&g_vmClientAsync.mutex);
        if (g_vmClientAsync.stopRequested ||
            completion->generation != g_vmClientAsync.generation)
        {
            pthread_mutex_unlock(&g_vmClientAsync.mutex);
            vm_client_free_completion(completion);
            continue;
        }
        if (g_vmClientAsync.completionTail != NULL)
            g_vmClientAsync.completionTail->next = completion;
        else
            g_vmClientAsync.completionHead = completion;
        g_vmClientAsync.completionTail = completion;
        pthread_mutex_unlock(&g_vmClientAsync.mutex);
    }
    free(responseScratch);
    free(followupScratch);
    return NULL;
}

static bool vm_client_ensure_worker(void)
{
    bool started;
    pthread_mutex_lock(&g_vmClientAsync.mutex);
    if (!g_vmClientAsync.workerStarted && !g_vmClientAsync.stopRequested &&
        pthread_create(&g_vmClientAsync.worker, NULL, vm_client_worker_main, NULL) == 0)
    {
        g_vmClientAsync.workerStarted = true;
        printf("[info][network] android client worker started queue_cap=%u\n",
               VM_CLIENT_QUEUE_MAX);
    }
    started = g_vmClientAsync.workerStarted;
    pthread_mutex_unlock(&g_vmClientAsync.mutex);
    return started;
}

static bool vm_client_enqueue(vm_client_job_kind kind, u32 connectId,
                              const u8 *request, u32 requestLen)
{
    vm_client_job *job;
    if (kind == VM_CLIENT_JOB_DATA &&
        (request == NULL || requestLen == 0 || requestLen > VM_CLIENT_REQUEST_MAX))
        return false;
    if (!vm_client_ensure_worker())
        return false;
    job = (vm_client_job *)calloc(1, sizeof(*job));
    if (job == NULL)
        return false;
    job->kind = kind;
    job->connectId = connectId;
    job->requestLen = requestLen;
    job->enqueueMs = SDL_GetTicks();
    if (requestLen != 0)
        memcpy(job->request, request, requestLen);

    pthread_mutex_lock(&g_vmClientAsync.mutex);
    if (g_vmClientAsync.stopRequested ||
        g_vmClientAsync.queuedJobs >= VM_CLIENT_QUEUE_MAX ||
        (kind == VM_CLIENT_JOB_SCENE_POLL && g_vmClientAsync.scenePollOutstanding))
    {
        pthread_mutex_unlock(&g_vmClientAsync.mutex);
        free(job);
        return false;
    }
    job->generation = g_vmClientAsync.generation;
    job->sequence = g_vmClientAsync.nextSequence++;
    if (g_vmClientAsync.nextSequence == 0)
        g_vmClientAsync.nextSequence = 1;
    if (g_vmClientAsync.jobTail != NULL)
        g_vmClientAsync.jobTail->next = job;
    else
        g_vmClientAsync.jobHead = job;
    g_vmClientAsync.jobTail = job;
    ++g_vmClientAsync.queuedJobs;
    if (kind == VM_CLIENT_JOB_SCENE_POLL)
        g_vmClientAsync.scenePollOutstanding = true;
    pthread_cond_signal(&g_vmClientAsync.condition);
    pthread_mutex_unlock(&g_vmClientAsync.mutex);
    return true;
}

static bool vm_client_remote_observation_needs_attach(
    const vm_net_remote_observation *observation)
{
    return observation != NULL && observation->hasHangupBattleStart;
}

/* Hangup parser tracing is opt-in diagnostics only.  The event still uses the
 * callback/context the CBE registered; this metadata never selects or changes
 * the callback. */
static bool vm_client_attach_remote_observation(
    u32 eventType, u32 responsePtr, u32 callback, u32 context,
    const vm_net_remote_observation *observation, const char *delivery)
{
    if (!vm_client_remote_observation_needs_attach(observation))
        return true;
    if (scheduler_attach_net_remote_observation(eventType, responsePtr,
                                                callback, context,
                                                observation))
    {
        return true;
    }
    printf("[warn][screen] remote_observation_attach_failed event=%u "
           "response=%08x delivery=%s action=no-early-global-mutation\n",
           eventType, responsePtr, delivery ? delivery : "unknown");
    return false;
}

/*
 * The post-shop hangup investigation needs the guest callback boundary, not
 * merely the TCP completion.  Recognise only the battle-start object prefix
 * emitted by the old direct builder (2/10, 2/2, 4/5, 4/11), the corrected
 * scene-poll start (2/2, 4/5, 4/11), the two-object scene start (2/2, 4/5)
 * used after an action13 confirmation, or a standalone PVP 4/10 start.  The
 * PVP form is included solely to prove whether the existing mmBattle module
 * consumes the packet after an arena/spar confirmation.  This is observation
 * only; transport scheduling and response bytes remain untouched.
 */
static void vm_client_capture_hangup_battle_start_response(
    const vm_client_completion *completion,
    vm_net_remote_observation *observation)
{
    const u8 *packet;
    u32 packetLen;
    u32 offset;
    u8 objectCount;
    u8 parsedCount = 0;
    vm_client_wt_object object;
    const u8 *expectedKinds = NULL;
    const u8 *expectedSubtypes = NULL;
    u8 expectedCount = 0;
    static const u8 directKinds[4] = {2, 2, 4, 4};
    static const u8 directSubtypes[4] = {10, 2, 5, 11};
    static const u8 pollKinds[3] = {2, 4, 4};
    static const u8 pollSubtypes[3] = {2, 5, 11};
    static const u8 sceneKinds[2] = {2, 4};
    static const u8 sceneSubtypes[2] = {2, 5};
    static const u8 pvpKinds[1] = {4};
    static const u8 pvpSubtypes[1] = {10};

    if (observation == NULL)
        return;
    if (completion == NULL || completion->eventType != 7 ||
        completion->response == NULL || completion->responseLen < 5)
    {
        return;
    }
    packet = completion->response;
    packetLen = ((u32)packet[2] << 8) | packet[3];
    if (packet[0] != 'W' || packet[1] != 'T' ||
        packetLen != completion->responseLen)
    {
        return;
    }
    /* Downlink WT keeps an outer object count at byte 4.  Its objects use a
     * six-byte header: major/kind/subtype/reserved/len-hi/len-lo.  Do not use
     * the five-byte request-object helper here. */
    objectCount = packet[4];
    if (objectCount == 0 || packetLen < 11)
        return;
    offset = 5;
    if (packet[offset] == 1 && packet[offset + 1] == 2 &&
        packet[offset + 2] == 10)
    {
        expectedKinds = directKinds;
        expectedSubtypes = directSubtypes;
        expectedCount = 4;
        observation->hangupBattleStartDirect = 1;
    }
    else if (packet[offset] == 1 && packet[offset + 1] == 2 &&
             packet[offset + 2] == 2)
    {
        if (objectCount == 2)
        {
            expectedKinds = sceneKinds;
            expectedSubtypes = sceneSubtypes;
            expectedCount = 2;
        }
        else
        {
            expectedKinds = pollKinds;
            expectedSubtypes = pollSubtypes;
            expectedCount = 3;
        }
    }
    else if (packet[offset] == 1 && packet[offset + 1] == 4 &&
             packet[offset + 2] == 10)
    {
        expectedKinds = pvpKinds;
        expectedSubtypes = pvpSubtypes;
        expectedCount = 1;
        observation->hangupBattleStartDirect = 1;
    }
    else
    {
        return;
    }
    if (objectCount < expectedCount)
        return;
    while (parsedCount < objectCount)
    {
        u16 objectLen;
        if (offset + 6 > packetLen)
            return;
        objectLen = (u16)(((u16)packet[offset + 4] << 8) |
                          packet[offset + 5]);
        if (objectLen < 6 || offset + objectLen > packetLen)
            return;
        object.major = packet[offset];
        object.kind = packet[offset + 1];
        object.subtype = packet[offset + 2];
        object.payloadLen = (u16)(objectLen - 6);
        if (parsedCount < expectedCount &&
            (object.major != 1 || object.kind != expectedKinds[parsedCount] ||
             object.subtype != expectedSubtypes[parsedCount]))
        {
            return;
        }
        ++parsedCount;
        offset += objectLen;
    }
    if (parsedCount != objectCount || offset != packetLen)
        return;

    observation->hasHangupBattleStart = 1;
    observation->hangupResponseObjectCount = objectCount;
    observation->hangupResponseParsedCount = parsedCount;
    observation->hangupResponseSequence = completion->sequence;
    observation->hangupResponseLength = completion->responseLen;
}

static void vm_net_mock_async_drain_completions(void)
{
    static u32 failureLogCount = 0;
    for (;;)
    {
        vm_client_completion *completion;
        vm_net_channel *channel;
        vm_net_remote_observation remoteObservation;
        u32 generation;
        u32 responsePtr;
        u32 nowMs;
        bool responseQueued = false;

        memset(&remoteObservation, 0, sizeof(remoteObservation));
        pthread_mutex_lock(&g_vmClientAsync.mutex);
        completion = g_vmClientAsync.completionHead;
        if (completion != NULL)
        {
            g_vmClientAsync.completionHead = completion->next;
            if (g_vmClientAsync.completionHead == NULL)
                g_vmClientAsync.completionTail = NULL;
            if (completion->kind == VM_CLIENT_JOB_SCENE_POLL)
                g_vmClientAsync.scenePollOutstanding = false;
        }
        generation = g_vmClientAsync.generation;
        pthread_mutex_unlock(&g_vmClientAsync.mutex);
        if (completion == NULL)
            break;
        if (completion->generation != generation)
        {
            vm_client_free_completion(completion);
            continue;
        }
        if (!completion->success)
        {
            if (failureLogCount < 8)
            {
                ++failureLogCount;
                printf("[warn][network] server request failed target=%s:%u kind=%s\n",
                       g_mockServiceHost, g_mockServicePort,
                       completion->kind == VM_CLIENT_JOB_SCENE_POLL ? "scene-poll" : "data");
            }
            vm_client_free_completion(completion);
            continue;
        }
        failureLogCount = 0;
        if (completion->responseLen == 0)
        {
            vm_client_free_completion(completion);
            continue;
        }
        channel = scheduler_find_net_channel(completion->connectId);
        if (channel == NULL || channel->callback == 0)
        {
            vm_client_free_completion(completion);
            continue;
        }
        responsePtr = vm_net_mock_sync_buffer_to_vm(completion->response,
                                                    completion->responseLen);
        if (responsePtr == 0)
        {
            vm_client_free_completion(completion);
            continue;
        }
        if (completion->responseLen <= sizeof(g_netMockResponse))
        {
            memcpy(g_netMockResponse, completion->response, completion->responseLen);
            g_netMockResponseLen = completion->responseLen;
            g_netMockResponseOffset = 0;
        }
        vm_client_capture_hangup_battle_start_response(completion,
                                                       &remoteObservation);
        vm_hangup_vital_forensics_capture_response(
            completion->response, completion->responseLen,
            completion->eventType, completion->sequence,
            responsePtr, channel->callback);
        vm_battle_insight_forensics_capture_response(
            completion->response, completion->responseLen,
            completion->eventType, completion->sequence,
            responsePtr, channel->callback, channel->context,
            completion->connectId);
        /* Scenario automation observes the exact downlink packet before it is
         * copied to guest RAM.  It never changes bytes, queues, callbacks or
         * scheduler ordering. */
        vm_automation_note_network_response(completion->response,
                                            completion->responseLen,
                                            completion->eventType,
                                            completion->sequence);
        vm_shop_return_forensics_note_downlink(completion->response,
                                               completion->responseLen,
                                               completion->eventType,
                                               completion->sequence,
                                               completion->connectId);
        g_netDownLinkData += completion->responseLen;
        responseQueued = scheduler_queue_net_event(
            completion->eventType, responsePtr, completion->responseLen,
            completion->responseLen, channel->callback, channel->context);
        if (responseQueued)
        {
            (void)vm_client_attach_remote_observation(
                completion->eventType, responsePtr, channel->callback,
                channel->context, &remoteObservation, "primary");
        }
        if (remoteObservation.hasHangupBattleStart)
        {
            printf("[info][network] mock_hangup_response_queue seq=%u "
                   "event=%u response=%u objects=%u parsed=%u connect=%u "
                   "delivery=normal source=remote-hangup-start\n",
                   remoteObservation.hangupResponseSequence,
                   completion->eventType,
                   remoteObservation.hangupResponseLength,
                   remoteObservation.hangupResponseObjectCount,
                   remoteObservation.hangupResponseParsedCount,
                   completion->connectId);
            vm_net_append_hangup_protocol_trace(
                "queue", &remoteObservation, completion->eventType,
                responsePtr, channel->callback, 0xffff, 0, UC_ERR_OK);
        }
        nowMs = SDL_GetTicks();
        printf("[info][network] queue_%s connect=%u event=%u resp=%u queue_ms=%u network_ms=%u deliver_ms=%u\n",
               completion->kind == VM_CLIENT_JOB_SCENE_POLL ? "scene_poll" : "data",
               completion->connectId, completion->eventType, completion->responseLen,
               completion->workerStartMs - completion->enqueueMs,
               completion->workerDoneMs - completion->workerStartMs,
               nowMs - completion->workerDoneMs);
        if (completion->followupLen != 0 && completion->followup != NULL)
        {
            u32 followupPtr = vm_net_mock_sync_buffer_to_vm(completion->followup,
                                                            completion->followupLen);
            if (followupPtr != 0)
            {
                bool followupQueued;

                followupQueued = scheduler_queue_net_event(
                    7, followupPtr, completion->followupLen,
                    completion->followupLen, channel->callback,
                    channel->context);
                if (completion->followupNextSchedulerTick)
                {
                    bool deferred = scheduler_defer_net_event_to_next_tick(
                        7, followupPtr, channel->callback, channel->context);
                    printf("[info][network] action13_battle_followup_queue "
                           "event=7 resp=%u queue_tick=%u eligible_tick=%u "
                           "deferred=%u cb=%08x ctx=%08x\n",
                           completion->followupLen, g_schedulerTick,
                           g_schedulerTick + 1u, deferred ? 1u : 0u,
                           channel->callback, channel->context);
                }
            }
            else if (completion->followupNextSchedulerTick)
            {
                printf("[info][network] action13_battle_followup_queue "
                       "event=7 resp=%u queue_tick=%u deferred=0 reason=vm-buffer\n",
                       completion->followupLen, g_schedulerTick);
            }
        }
        if (completion->closeAfterData)
            scheduler_queue_net_event(9, 0, 0, 0, channel->callback, channel->context);
        vm_client_free_completion(completion);
    }
}

static void vm_net_mock_async_reset(void)
{
    vm_client_job *job;
    vm_client_completion *completion;
    pthread_mutex_lock(&g_vmClientAsync.mutex);
    ++g_vmClientAsync.generation;
    if (g_vmClientAsync.generation == 0)
        g_vmClientAsync.generation = 1;
    job = g_vmClientAsync.jobHead;
    completion = g_vmClientAsync.completionHead;
    g_vmClientAsync.jobHead = NULL;
    g_vmClientAsync.jobTail = NULL;
    g_vmClientAsync.completionHead = NULL;
    g_vmClientAsync.completionTail = NULL;
    g_vmClientAsync.queuedJobs = 0;
    g_vmClientAsync.scenePollOutstanding = false;
    pthread_mutex_unlock(&g_vmClientAsync.mutex);
    while (job != NULL)
    {
        vm_client_job *next = job->next;
        free(job);
        job = next;
    }
    while (completion != NULL)
    {
        vm_client_completion *next = completion->next;
        vm_client_free_completion(completion);
        completion = next;
    }
}

static void vm_net_mock_async_shutdown(void)
{
    bool joinWorker;
    pthread_mutex_lock(&g_vmClientAsync.mutex);
    joinWorker = g_vmClientAsync.workerStarted;
    g_vmClientAsync.stopRequested = true;
    pthread_cond_broadcast(&g_vmClientAsync.condition);
    pthread_mutex_unlock(&g_vmClientAsync.mutex);
    if (joinWorker)
        pthread_join(g_vmClientAsync.worker, NULL);
    vm_net_mock_async_reset();
    pthread_mutex_lock(&g_vmClientAsync.mutex);
    g_vmClientAsync.workerStarted = false;
    pthread_mutex_unlock(&g_vmClientAsync.mutex);
}

static void vm_net_mock_on_send(u32 connectId, u32 dataPtr, u32 dataLen)
{
    u8 request[VM_CLIENT_REQUEST_MAX];
    u32 readLen;
    if (dataPtr == 0 || dataLen == 0)
        return;
    readLen = dataLen < sizeof(request) ? dataLen : sizeof(request);
    if (uc_mem_read(MTK, dataPtr, request, readLen) != UC_ERR_OK)
        return;
    vm_shop_return_forensics_note_uplink(request, readLen, connectId);
    vm_automation_note_uplink(request, readLen);
    if (!vm_client_enqueue(VM_CLIENT_JOB_DATA, connectId, request, readLen))
    {
        printf("[warn][network] client queue full connect=%u len=%u\n",
               connectId, readLen);
        return;
    }
    g_netUpLinkData += dataLen;
}

static void vm_net_mock_poll_push_if_due(void)
{
    static u32 lastPollTick = 0;
    vm_net_channel *channel = NULL;
    if (g_mockServiceClientId == 0 || Global_R9 == 0)
        return;
    if (lastPollTick != 0 && g_schedulerTick - lastPollTick < 1)
        return;
    for (u32 i = 0; i < VM_SCHED_MAX_NET_TASKS; ++i)
    {
        if (g_netChannels[i].active && g_netChannels[i].callback != 0)
        {
            channel = &g_netChannels[i];
            break;
        }
    }
    if (channel == NULL)
        return;
    if (scheduler_find_pending_net_event(7, channel->callback,
                                         channel->context) != NULL)
        return;
    if (vm_client_enqueue(VM_CLIENT_JOB_SCENE_POLL, channel->connectId, NULL, 0))
        lastPollTick = g_schedulerTick;
}

static u32 vm_net_mock_read_data(u32 dst, u32 dstLen)
{
    u32 remain;
    u32 copyLen;
    if (dst == 0 || dstLen == 0 || g_netMockResponseOffset >= g_netMockResponseLen)
        return vm_set_call_result(0);
    remain = g_netMockResponseLen - g_netMockResponseOffset;
    copyLen = dstLen < remain ? dstLen : remain;
    uc_mem_write(MTK, dst, g_netMockResponse + g_netMockResponseOffset, copyLen);
    g_netMockResponseOffset += copyLen;
    return vm_set_call_result(copyLen);
}
