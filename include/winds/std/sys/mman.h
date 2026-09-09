#ifndef winds_sys_mman_h
#define winds_sys_mman_h

#include <stddef.h>
#include <sys/types.h>

#define PROT_READ 1
#define PROT_WRITE 2
#define MAP_SHARED 1
#define MAP_PRIVATE 2
#define MAP_FAILED ((void *)-1)

extern void *mmap(void *address, size_t length, int protection, int flags, int fd, off_t offset);
extern int munmap(void *address, size_t length);

#endif
