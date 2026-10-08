#ifndef VM_MYSQL_CLIENT_H
#define VM_MYSQL_CLIENT_H

#include <stdbool.h>
#include <stddef.h>

typedef bool (*vm_mysql_row_callback)(void *context,
                                      unsigned int column_count,
                                      const char *const *values,
                                      const size_t *lengths);

bool vm_mysql_exec(const char *sql);
bool vm_mysql_query(const char *sql, vm_mysql_row_callback callback, void *context);
const char *vm_mysql_last_error(void);
void vm_mysql_close(void);

size_t vm_mysql_hex_encode(const void *data, size_t data_len, char *output, size_t output_size);
bool vm_mysql_hex_decode(const char *text, size_t text_len, void *output, size_t output_size, size_t *decoded_len);

#endif
