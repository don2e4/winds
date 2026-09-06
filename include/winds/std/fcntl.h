#ifndef winds_fcntl_h
#define winds_fcntl_h

#define O_RDONLY 0
#define O_WRONLY 1
#define O_RDWR 2
#define O_CREAT 64
#define O_EXCL 128
#define O_TRUNC 512
#define O_APPEND 1024
#define O_CLOEXEC 524288

extern int open(const char *path, int flags, ...);

#endif
