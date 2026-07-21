#ifndef VM_MD5_H
#define VM_MD5_H

#include <stddef.h>

void vm_md5_hex(const void *data, size_t length, char output[33]);

#endif
