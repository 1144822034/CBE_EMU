/*
 * Protocol-only regression for mall secret-treasure pagination.
 *
 * It seeds twelve secret items in the in-process catalog, then submits the
 * client-native WT 1/14/5 requests for index 0 and index 1.  No listener,
 * database, client memory, or CBM binary is touched.
 */

#include <stdio.h>
#include <string.h>

#define main cbe_server_program_main
#include "../src/server_main.c"
#undef main

static u32 append_request_object(u8 *request, u32 pos,
                                 u8 major, u8 kind, u8 subtype,
                                 const u8 *payload, u16 payloadLen)
{
    u32 objectLen = 5u + payloadLen;

    request[pos] = major;
    request[pos + 1] = kind;
    request[pos + 2] = subtype;
    request[pos + 3] = (u8)(objectLen >> 8);
    request[pos + 4] = (u8)objectLen;
    if (payloadLen != 0 && payload != NULL)
        memcpy(request + pos + 5, payload, payloadLen);
    return pos + objectLen;
}

static u32 make_secret_page_request(u8 *request, u8 index)
{
    u8 payload[32];
    u32 payloadLen = 0;
    u32 pos = 4;

    if (!vm_net_mock_put_object_u8(payload, sizeof(payload), &payloadLen,
                                   "index", index))
    {
        return 0;
    }
    request[0] = 'W';
    request[1] = 'T';
    pos = append_request_object(request, pos, 1, 14, 5, payload,
                                (u16)payloadLen);
    request[2] = (u8)(pos >> 8);
    request[3] = (u8)pos;
    return pos;
}

static u32 make_secret_page_batch_request(u8 *request, u8 firstIndex,
                                          u8 secondIndex)
{
    u8 payload[32];
    u32 payloadLen = 0;
    u32 pos = 4;

    request[0] = 'W';
    request[1] = 'T';
    if (!vm_net_mock_put_object_u8(payload, sizeof(payload), &payloadLen,
                                   "index", firstIndex))
    {
        return 0;
    }
    pos = append_request_object(request, pos, 1, 14, 5, payload,
                                (u16)payloadLen);
    payloadLen = 0;
    if (!vm_net_mock_put_object_u8(payload, sizeof(payload), &payloadLen,
                                   "index", secondIndex))
    {
        return 0;
    }
    pos = append_request_object(request, pos, 1, 14, 5, payload,
                                (u16)payloadLen);
    request[2] = (u8)(pos >> 8);
    request[3] = (u8)pos;
    return pos;
}

static bool decode_secret_page_response_object(const u8 *response,
                                               u32 responseLen, u32 *offset,
                                               u32 *totalOut, u8 *rowsOut,
                                               u32 *firstItemIdOut)
{
    u32 objectLen = 0;
    const u8 *payload = NULL;
    u32 payloadLen = 0;
    const u8 *itemInfo = NULL;
    u16 itemInfoLen = 0;
    const u8 *totalValue = NULL;
    u16 totalValueLen = 0;
    u32 total = 0;

    if (totalOut)
        *totalOut = 0;
    if (rowsOut)
        *rowsOut = 0;
    if (firstItemIdOut)
        *firstItemIdOut = 0;
    if (response == NULL || offset == NULL || *offset + 6 > responseLen ||
        response[*offset] != 1 || response[*offset + 1] != 14 ||
        response[*offset + 2] != 5)
    {
        return false;
    }

    objectLen = ((u32)response[*offset + 4] << 8) | response[*offset + 5];
    if (objectLen < 6 || *offset + objectLen > responseLen)
        return false;
    payload = response + *offset + 6;
    payloadLen = objectLen - 6;
    if (!vm_net_mock_get_object_entry_bytes(payload, payloadLen, "totalnum",
                                            &totalValue, &totalValueLen) ||
        totalValue == NULL || totalValueLen != 6 || totalValue[0] != 0 ||
        totalValue[1] != 4 ||
        !vm_net_mock_get_object_entry_bytes(payload, payloadLen, "iteminfo",
                                            &itemInfo, &itemInfoLen) ||
        itemInfo == NULL || itemInfoLen == 0)
    {
        return false;
    }

    total = ((u32)totalValue[2] << 24) |
            ((u32)totalValue[3] << 16) |
            ((u32)totalValue[4] << 8) |
            totalValue[5];

    if (itemInfoLen < 9 || itemInfo[0] != 0 || itemInfo[1] != 1 ||
        itemInfo[3] != 0 || itemInfo[4] != 4)
    {
        return false;
    }

    if (totalOut)
        *totalOut = total;
    if (rowsOut)
        *rowsOut = itemInfo[2];
    if (firstItemIdOut)
    {
        *firstItemIdOut = ((u32)itemInfo[5] << 24) |
                          ((u32)itemInfo[6] << 16) |
                          ((u32)itemInfo[7] << 8) |
                          itemInfo[8];
    }
    *offset += objectLen;
    return true;
}

static bool decode_secret_page_response(const u8 *response, u32 responseLen,
                                        u32 *totalOut, u8 *rowsOut,
                                        u32 *firstItemIdOut)
{
    u32 offset = 5;

    return response != NULL && responseLen >= 11 && response[0] == 'W' &&
           response[1] == 'T' && response[4] == 1 &&
           decode_secret_page_response_object(response, responseLen, &offset,
                                              totalOut, rowsOut,
                                              firstItemIdOut) &&
           offset == responseLen;
}

static bool decode_secret_page_batch_response(const u8 *response,
                                              u32 responseLen,
                                              u32 *firstTotalOut,
                                              u8 *firstRowsOut,
                                              u32 *firstItemIdOut,
                                              u32 *secondTotalOut,
                                              u8 *secondRowsOut,
                                              u32 *secondItemIdOut)
{
    u32 offset = 5;

    return response != NULL && responseLen >= 17 && response[0] == 'W' &&
           response[1] == 'T' && response[4] == 2 &&
           decode_secret_page_response_object(response, responseLen, &offset,
                                              firstTotalOut, firstRowsOut,
                                              firstItemIdOut) &&
           decode_secret_page_response_object(response, responseLen, &offset,
                                              secondTotalOut, secondRowsOut,
                                              secondItemIdOut) &&
           offset == responseLen;
}

int main(void)
{
    u8 request[64];
    u8 response[4096];
    u32 requestLen = 0;
    u32 responseLen = 0;
    u32 total = 0;
    u32 firstItemId = 0;
    u32 secondTotal = 0;
    u32 secondItemId = 0;
    u8 rows = 0;
    u8 secondRows = 0;
    bool decoded = false;

    memset(g_vm_net_mock_shop_catalog, 0,
           sizeof(g_vm_net_mock_shop_catalog));
    for (u32 i = 0; i < 12; ++i)
    {
        vm_net_mock_shop_catalog_item *item =
            &g_vm_net_mock_shop_catalog[i];

        item->itemId = 800u + i;
        snprintf(item->name, sizeof(item->name), "secret-%u", i);
        item->price = 10u + i;
        item->stock = 99;
        item->stack = 1;
        item->visual = 1;
        item->category = 14;
        item->enabled = 1;
        item->shopSection = VM_NET_MOCK_SHOP_SECTION_SECRET;
    }
    g_vm_net_mock_shop_catalog_count = 12;
    g_vm_net_mock_shop_catalog_loaded = true;
    g_vm_net_mock_shop_admin_db_loaded = true;
    g_vm_net_mock_shop_admin_db_valid = false;

    requestLen = make_secret_page_request(request, 0);
    responseLen = vm_net_mock_build_shop_page14_response(
        request, requestLen, response, sizeof(response));
    decoded = decode_secret_page_response(response, responseLen, &total, &rows,
                                          &firstItemId);
    if (requestLen == 0 || responseLen == 0 || !decoded ||
        total != 12 || rows != VM_NET_MOCK_SHOP_PAGE_SIZE ||
        firstItemId != 800)
    {
        fprintf(stderr, "secret page 0 decode=%u total=%u rows=%u first=%u len=%u\n",
                decoded ? 1 : 0, total, rows, firstItemId, responseLen);
        fputs("secret page 0 did not expose the full catalog total\n", stderr);
        return 1;
    }

    requestLen = make_secret_page_request(request, 1);
    responseLen = vm_net_mock_build_shop_page14_response(
        request, requestLen, response, sizeof(response));
    decoded = decode_secret_page_response(response, responseLen, &total, &rows,
                                          &firstItemId);
    if (requestLen == 0 || responseLen == 0 || !decoded ||
        total != 12 || rows != 2 || firstItemId != 810)
    {
        fprintf(stderr, "secret page 1 decode=%u total=%u rows=%u first=%u len=%u\n",
                decoded ? 1 : 0, total, rows, firstItemId, responseLen);
        fputs("secret page 1 did not expose the remaining catalog items\n",
              stderr);
        return 1;
    }

    requestLen = make_secret_page_batch_request(request, 0, 1);
    responseLen = vm_net_mock_build_shop_page14_response(
        request, requestLen, response, sizeof(response));
    decoded = decode_secret_page_batch_response(
        response, responseLen, &total, &rows, &firstItemId, &secondTotal,
        &secondRows, &secondItemId);
    if (requestLen == 0 || responseLen == 0 || !decoded || total != 12 ||
        rows != VM_NET_MOCK_SHOP_PAGE_SIZE || firstItemId != 800 ||
        secondTotal != 12 || secondRows != 2 || secondItemId != 810)
    {
        fprintf(stderr, "secret batch decode=%u first=%u/%u/%u second=%u/%u/%u len=%u\n",
                decoded ? 1 : 0, total, rows, firstItemId, secondTotal,
                secondRows, secondItemId, responseLen);
        fputs("secret page batch did not preserve both requested pages\n",
              stderr);
        return 1;
    }

    puts("shop secret-pagination regression passed: total=12 pages=10+2");
    return 0;
}
