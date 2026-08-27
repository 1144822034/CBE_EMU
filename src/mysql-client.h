#ifndef VM_MYSQL_CLIENT_H
#define VM_MYSQL_CLIENT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef bool (*vm_mysql_row_callback)(void *context,
                                      unsigned int column_count,
                                      const char *const *values,
                                      const size_t *lengths);

bool vm_mysql_exec(const char *sql);
bool vm_mysql_query(const char *sql, vm_mysql_row_callback callback, void *context);
/* Sends a native COM_PING only on this thread's already-open connection.
 * It never opens a connection merely to keep it alive. */
bool vm_mysql_keepalive(void);
const char *vm_mysql_last_error(void);
void vm_mysql_close(void);

size_t vm_mysql_hex_encode(const void *data, size_t data_len, char *output, size_t output_size);
bool vm_mysql_hex_decode(const char *text, size_t text_len, void *output, size_t output_size, size_t *decoded_len);
/* Strict decimal parser for unsigned MySQL result fields.  It is shared by
 * independent service protocol modules and rejects empty, signed and
 * overflowing values. */
bool vm_mock_mysql_parse_u32(const char *value, size_t value_len,
                             uint32_t *result_out);

#endif
