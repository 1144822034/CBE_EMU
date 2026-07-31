#pragma once

#include <stdarg.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Runtime log mode for host printf traffic (tagged [debug]/[info]/[warn]/[error]).
 *
 * production: warn + error only (default; keeps hot paths quiet)
 * debug:      all levels including untagged lines
 * quiet:      error only
 * off:        silence everything
 *
 * Configure with CBE_LOG_MODE / CBE_LOG_LEVEL, --log-mode=, --debug-log, --quiet-log.
 */
typedef enum cbe_log_mode {
    CBE_LOG_MODE_PRODUCTION = 0,
    CBE_LOG_MODE_DEBUG = 1,
    CBE_LOG_MODE_QUIET = 2,
    CBE_LOG_MODE_OFF = 3
} cbe_log_mode_t;

typedef enum cbe_log_level {
    CBE_LOG_LEVEL_DEBUG = 0,
    CBE_LOG_LEVEL_INFO = 1,
    CBE_LOG_LEVEL_WARN = 2,
    CBE_LOG_LEVEL_ERROR = 3
} cbe_log_level_t;

void cbe_log_set_mode(cbe_log_mode_t mode);
cbe_log_mode_t cbe_log_get_mode(void);
const char *cbe_log_mode_name(cbe_log_mode_t mode);
int cbe_log_parse_mode(const char *text, cbe_log_mode_t *outMode);

/* Apply compile default, then CBE_LOG_MODE / CBE_LOG_LEVEL. */
void cbe_log_apply_env(void);
/* Apply env, then argv flags. Safe to call with argc<=0. */
void cbe_log_init_from_args(int argc, char *argv[]);

int cbe_log_level_of_fmt(const char *fmt);
int cbe_log_should_print(const char *fmt);
int cbe_log_vprintf(const char *fmt, va_list ap);
int cbe_log_printf(const char *fmt, ...);

#ifdef __cplusplus
}
#endif
