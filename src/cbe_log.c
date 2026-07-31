#include "cbe_log.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(CBE_LOG_DEFAULT_DEBUG)
static volatile int g_cbe_log_mode = CBE_LOG_MODE_DEBUG;
#else
/* Prefer quiet hot paths unless developers explicitly enable debug logging. */
static volatile int g_cbe_log_mode = CBE_LOG_MODE_PRODUCTION;
#endif

void cbe_log_set_mode(cbe_log_mode_t mode)
{
    if (mode < CBE_LOG_MODE_PRODUCTION || mode > CBE_LOG_MODE_OFF)
        mode = CBE_LOG_MODE_PRODUCTION;
    g_cbe_log_mode = (int)mode;
}

cbe_log_mode_t cbe_log_get_mode(void)
{
    return (cbe_log_mode_t)g_cbe_log_mode;
}

const char *cbe_log_mode_name(cbe_log_mode_t mode)
{
    switch (mode)
    {
    case CBE_LOG_MODE_DEBUG:
        return "debug";
    case CBE_LOG_MODE_QUIET:
        return "quiet";
    case CBE_LOG_MODE_OFF:
        return "off";
    case CBE_LOG_MODE_PRODUCTION:
    default:
        return "production";
    }
}

int cbe_log_parse_mode(const char *text, cbe_log_mode_t *outMode)
{
    cbe_log_mode_t mode;

    if (text == NULL || text[0] == 0 || outMode == NULL)
        return 0;
    if (strcmp(text, "production") == 0 || strcmp(text, "prod") == 0 ||
        strcmp(text, "release") == 0)
        mode = CBE_LOG_MODE_PRODUCTION;
    else if (strcmp(text, "debug") == 0 || strcmp(text, "verbose") == 0 ||
             strcmp(text, "all") == 0)
        mode = CBE_LOG_MODE_DEBUG;
    else if (strcmp(text, "quiet") == 0 || strcmp(text, "error") == 0)
        mode = CBE_LOG_MODE_QUIET;
    else if (strcmp(text, "off") == 0 || strcmp(text, "none") == 0 ||
             strcmp(text, "0") == 0)
        mode = CBE_LOG_MODE_OFF;
    else if (strcmp(text, "warn") == 0 || strcmp(text, "warning") == 0 ||
             strcmp(text, "info") == 0)
    {
        /* Level aliases map onto the nearest mode. */
        if (strcmp(text, "info") == 0)
            mode = CBE_LOG_MODE_DEBUG;
        else
            mode = CBE_LOG_MODE_PRODUCTION;
    }
    else
        return 0;
    *outMode = mode;
    return 1;
}

void cbe_log_apply_env(void)
{
    const char *envMode = getenv("CBE_LOG_MODE");
    const char *envLevel = getenv("CBE_LOG_LEVEL");
    cbe_log_mode_t mode;

    if (envMode != NULL && envMode[0] != 0 &&
        cbe_log_parse_mode(envMode, &mode))
        cbe_log_set_mode(mode);
    else if (envLevel != NULL && envLevel[0] != 0 &&
             cbe_log_parse_mode(envLevel, &mode))
        cbe_log_set_mode(mode);
}

void cbe_log_init_from_args(int argc, char *argv[])
{
    int i;
    cbe_log_mode_t mode;

    cbe_log_apply_env();
    if (argc <= 0 || argv == NULL)
        return;
    for (i = 1; i < argc; ++i)
    {
        if (argv[i] == NULL)
            continue;
        if (strcmp(argv[i], "--debug-log") == 0 ||
            strcmp(argv[i], "--verbose-log") == 0)
            cbe_log_set_mode(CBE_LOG_MODE_DEBUG);
        else if (strcmp(argv[i], "--quiet-log") == 0)
            cbe_log_set_mode(CBE_LOG_MODE_QUIET);
        else if (strcmp(argv[i], "--no-log") == 0)
            cbe_log_set_mode(CBE_LOG_MODE_OFF);
        else if (strncmp(argv[i], "--log-mode=", 11) == 0 &&
                 cbe_log_parse_mode(argv[i] + 11, &mode))
            cbe_log_set_mode(mode);
        else if (strncmp(argv[i], "--log-level=", 12) == 0 &&
                 cbe_log_parse_mode(argv[i] + 12, &mode))
            cbe_log_set_mode(mode);
    }
}

int cbe_log_level_of_fmt(const char *fmt)
{
    if (fmt == NULL || fmt[0] != '[')
        return CBE_LOG_LEVEL_INFO;
    if (strncmp(fmt, "[error]", 7) == 0)
        return CBE_LOG_LEVEL_ERROR;
    if (strncmp(fmt, "[warn]", 6) == 0)
        return CBE_LOG_LEVEL_WARN;
    if (strncmp(fmt, "[info]", 6) == 0)
        return CBE_LOG_LEVEL_INFO;
    if (strncmp(fmt, "[debug]", 7) == 0)
        return CBE_LOG_LEVEL_DEBUG;
    return CBE_LOG_LEVEL_INFO;
}

int cbe_log_should_print(const char *fmt)
{
    cbe_log_mode_t mode = cbe_log_get_mode();
    int level;

    if (mode == CBE_LOG_MODE_OFF)
        return 0;
    if (mode == CBE_LOG_MODE_DEBUG)
        return 1;
    level = cbe_log_level_of_fmt(fmt);
    if (mode == CBE_LOG_MODE_QUIET)
        return level >= CBE_LOG_LEVEL_ERROR;
    /* production */
    return level >= CBE_LOG_LEVEL_WARN;
}

int cbe_log_vprintf(const char *fmt, va_list ap)
{
    if (!cbe_log_should_print(fmt))
        return 0;
    return vfprintf(stdout, fmt, ap);
}

int cbe_log_printf(const char *fmt, ...)
{
    va_list ap;
    int rc;

    if (!cbe_log_should_print(fmt))
        return 0;
    va_start(ap, fmt);
    rc = vfprintf(stdout, fmt, ap);
    va_end(ap);
    return rc;
}
