#ifndef winds_fcntl_h
#define winds_fcntl_h

#include <sys/types.h>

#define O_RDONLY 0
#define O_WRONLY 1
#define O_RDWR 2
#define O_CREAT 64
#define O_EXCL 128
#define O_TRUNC 512
#define O_APPEND 1024
#define O_CLOEXEC 524288
#define F_GETFD 1
#define F_SETFD 2
#define F_GETLK 5
#define F_SETLK 6
#define F_RDLCK 0
#define F_WRLCK 1
#define F_UNLCK 2

struct flock {
    short l_type, l_whence;
    off_t l_start, l_len;
    pid_t l_pid;
};

extern int open(const char *path, int flags, ...);
extern int fcntl(int fd, int op, ...);

#endif
