#ifndef winds_unistd_h
#define winds_unistd_h

#include <sys/types.h>
#include <stddef.h>

#define SEEK_SET 0
#define SEEK_CUR 1
#define SEEK_END 2

extern ssize_t read(int fd, void *buffer, size_t count);
extern ssize_t write(int fd, const void *buffer, size_t count);
extern int close(int fd);
extern off_t lseek(int fd, off_t offset, int whence);

#endif
