/*
 * Regression for the risk-admin audit pager.
 *
 * The risk page is the only admin tab whose row source is a MySQL table, so
 * this test renders the real page against the local development database
 * (SELECT-only; schema preparation is the same idempotent CREATE TABLE IF NOT
 * EXISTS the page itself runs).  It asserts the page/offset contract instead
 * of the old fixed 100-row dump:
 *   - the page parameter is parsed strictly and falls back to 1 on junk;
 *   - an out-of-range page clamps to the last page;
 *   - the pager bar reports 第 X / Y 页 · 共 N 条 with Y = ceil(N/50);
 *   - per-page row count never exceeds the page size;
 *   - adjacent pages never repeat a row (the offset moves past the window).
 * No listener or persistent state is started.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define main cbe_server_program_main
#include "../src/main.c"
#undef main

#define RISK_ROW_MARKER "<tr><td class=\"mono\">"
#define RISK_TIMESTAMP_LEN 26

static u32 count_occurrences(const char *text, const char *needle)
{
    u32 count = 0;
    size_t needleLen = needle ? strlen(needle) : 0;

    if (text == NULL || needleLen == 0)
        return 0;
    while ((text = strstr(text, needle)) != NULL)
    {
        ++count;
        text += needleLen;
    }
    return count;
}

static bool find_page_info(const char *text, u32 *pageOut, u32 *pageCountOut,
                           u32 *totalOut)
{
    const char *cursor = strstr(text, "第 ");

    if (pageOut != NULL)
        *pageOut = 0;
    if (pageCountOut != NULL)
        *pageCountOut = 0;
    if (totalOut != NULL)
        *totalOut = 0;
    if (cursor == NULL)
        return false;
    return sscanf(cursor, "第 %u / %u 页 · 共 %u 条",
                  pageOut, pageCountOut, totalOut) == 3;
}

/* Collects the created_at cell (26 chars, '%Y-%m-%d %H:%i:%s.%f') of each
 * rendered row so adjacent pages can be checked for overlap. */
static u32 collect_timestamps(const char *text, char stamps[][RISK_TIMESTAMP_LEN + 1],
                              u32 cap)
{
    const char *cursor = text;
    u32 count = 0;

    while (count < cap && (cursor = strstr(cursor, RISK_ROW_MARKER)) != NULL)
    {
        cursor += strlen(RISK_ROW_MARKER);
        memcpy(stamps[count], cursor, RISK_TIMESTAMP_LEN);
        stamps[count][RISK_TIMESTAMP_LEN] = 0;
        ++count;
    }
    return count;
}

static bool sets_overlap(const char (*left)[RISK_TIMESTAMP_LEN + 1], u32 leftCount,
                         const char (*right)[RISK_TIMESTAMP_LEN + 1], u32 rightCount)
{
    for (u32 i = 0; i < leftCount; ++i)
        for (u32 j = 0; j < rightCount; ++j)
            if (memcmp(left[i], right[j], RISK_TIMESTAMP_LEN) == 0)
                return true;
    return false;
}

static char *render_page(const char *query)
{
    char *page = (char *)calloc(1, VM_MOCK_ADMIN_RESPONSE_MAX);

    if (page == NULL)
        return NULL;
    vm_mock_admin_render_risk_page(page, VM_MOCK_ADMIN_RESPONSE_MAX, query);
    return page;
}

static bool page_ok(char *page, u32 *pageOut, u32 *pageCountOut, u32 *totalOut)
{
    if (page == NULL || strstr(page, "风险角色管理页面超过大小限制") != NULL ||
        strstr(page, "无法读取风险审计") != NULL ||
        strlen(page) >= VM_MOCK_ADMIN_RESPONSE_MAX)
    {
        return false;
    }
    return find_page_info(page, pageOut, pageCountOut, totalOut);
}

int main(void)
{
    char *page1 = NULL;
    char *page2 = NULL;
    char *junk = NULL;
    char *clamp = NULL;
    u32 page = 0;
    u32 pageCount = 0;
    u32 total = 0;
    u32 expectedPageCount = 0;
    char page1Stamps[VM_MOCK_ADMIN_RISK_AUDIT_PAGE_SIZE][RISK_TIMESTAMP_LEN + 1];
    char page2Stamps[VM_MOCK_ADMIN_RISK_AUDIT_PAGE_SIZE][RISK_TIMESTAMP_LEN + 1];
    u32 page1Rows = 0;
    u32 page2Rows = 0;
    bool failed = false;

    page1 = render_page("tab=risk");
    if (page1 == NULL)
    {
        fputs("unable to allocate admin response buffer\n", stderr);
        return 1;
    }
    if (strstr(page1, "无法读取风险审计") != NULL)
    {
        printf("risk admin pagination regression skipped: local MySQL unavailable (%s)\n",
               vm_mysql_last_error());
        free(page1);
        return 0;
    }
    if (strstr(page1, "风险审计列表") == NULL ||
        strstr(page1, "风险角色管理页面超过大小限制") != NULL)
    {
        fputs("risk page render failed before the pager could be checked\n", stderr);
        failed = true;
        goto done;
    }
    if (strstr(page1, "暂无三秒内连续进入战斗的审计记录") != NULL)
    {
        /* Empty trail: no pager may be emitted, but junk and out-of-range
         * page parameters must still render the same empty page instead of
         * erroring out or emitting a pager. */
        junk = render_page("tab=risk&page=abc");
        clamp = render_page("tab=risk&page=4294967295");
        if (junk == NULL || clamp == NULL ||
            strstr(junk, "暂无三秒内连续进入战斗的审计记录") == NULL ||
            strstr(junk, "第 ") != NULL ||
            strstr(clamp, "暂无三秒内连续进入战斗的审计记录") == NULL ||
            strstr(clamp, "第 ") != NULL ||
            strstr(junk, "风险角色管理页面超过大小限制") != NULL ||
            strstr(clamp, "风险角色管理页面超过大小限制") != NULL)
        {
            fputs("empty trail handled page parameters incorrectly\n", stderr);
            free(page1);
            free(junk);
            free(clamp);
            return 1;
        }
        free(page1);
        free(junk);
        free(clamp);
        printf("risk admin pagination regression passed (empty table): no rows rendered, no pager\n");
        return 0;
    }
    if (!page_ok(page1, &page, &pageCount, &total) || page != 1)
    {
        fputs("page 1 pager contract violated\n", stderr);
        failed = true;
        goto done;
    }
    expectedPageCount = (total + VM_MOCK_ADMIN_RISK_AUDIT_PAGE_SIZE - 1) /
                        VM_MOCK_ADMIN_RISK_AUDIT_PAGE_SIZE;
    if (pageCount != expectedPageCount)
    {
        fputs("page count does not match ceil(total/page-size)\n", stderr);
        failed = true;
        goto done;
    }
    page1Rows = collect_timestamps(page1, page1Stamps,
                                   VM_MOCK_ADMIN_RISK_AUDIT_PAGE_SIZE);
    if (page1Rows != (total < VM_MOCK_ADMIN_RISK_AUDIT_PAGE_SIZE
                          ? total : VM_MOCK_ADMIN_RISK_AUDIT_PAGE_SIZE))
    {
        fputs("page 1 row count is not min(total, page-size)\n", stderr);
        failed = true;
        goto done;
    }
    if (pageCount > 1 &&
        strstr(page1, "href=\"/?tab=risk&amp;page=2\">下一页</a>") == NULL)
    {
        fputs("page 1 is missing the next-page link\n", stderr);
        failed = true;
        goto done;
    }

    page2 = render_page("tab=risk&page=2");
    if (!page_ok(page2, &page, &pageCount, &total) || page != 2)
    {
        fputs("page 2 pager contract violated\n", stderr);
        failed = true;
        goto done;
    }
    page2Rows = collect_timestamps(page2, page2Stamps,
                                   VM_MOCK_ADMIN_RISK_AUDIT_PAGE_SIZE);
    if (total > VM_MOCK_ADMIN_RISK_AUDIT_PAGE_SIZE)
    {
        if (page2Rows != (total - VM_MOCK_ADMIN_RISK_AUDIT_PAGE_SIZE <
                                  VM_MOCK_ADMIN_RISK_AUDIT_PAGE_SIZE
                              ? total - VM_MOCK_ADMIN_RISK_AUDIT_PAGE_SIZE
                              : VM_MOCK_ADMIN_RISK_AUDIT_PAGE_SIZE))
        {
            fputs("page 2 row count is not min(total-page-size, page-size)\n",
                  stderr);
            failed = true;
            goto done;
        }
        if (sets_overlap(page1Stamps, page1Rows, page2Stamps, page2Rows))
        {
            fputs("page 2 repeats a row already shown on page 1\n", stderr);
            failed = true;
            goto done;
        }
        if (strstr(page2, "封号并断开") != NULL &&
            strstr(page2, "name=\"page\" value=\"2\"") == NULL)
        {
            fputs("ban form does not carry the current page back\n", stderr);
            failed = true;
            goto done;
        }
    }
    else
    {
        /* Only one page exists; a page=2 request must clamp to it. */
        if (strstr(page2, "第 1 / 1 页") == NULL)
        {
            fputs("page 2 on a single-page trail did not clamp to page 1\n",
                  stderr);
            failed = true;
            goto done;
        }
    }

    junk = render_page("tab=risk&page=abc");
    if (!page_ok(junk, &page, &pageCount, &total) || page != 1)
    {
        fputs("junk page parameter did not fall back to page 1\n", stderr);
        failed = true;
        goto done;
    }
    clamp = render_page("tab=risk&page=4294967295");
    if (!page_ok(clamp, &page, &pageCount, &total) || page != pageCount ||
        strstr(clamp, "下一页") != NULL)
    {
        fputs("out-of-range page did not clamp to the last page\n", stderr);
        failed = true;
        goto done;
    }

done:
    free(page1);
    free(page2);
    free(junk);
    free(clamp);
    if (failed)
        return 1;
    printf("risk admin pagination regression passed: total=%u pages=%u page1Rows=%u page2Rows=%u\n",
           total, pageCount, page1Rows, page2Rows);
    return 0;
}
