#ifndef winds_sys_stat_h
#define winds_sys_stat_h

#include <sys/types.h>
#include <time.h>

struct stat {
    dev_t st_dev;
    ino_t st_ino;
    nlink_t st_nlink;
    mode_t st_mode;
    uid_t st_uid;
    gid_t st_gid;
    int __pad0;
    dev_t st_rdev;
    off_t st_size;
    blksize_t st_blksize;
    blkcnt_t st_blocks;
    struct timespec st_atim, st_mtim, st_ctim;
    long __reserved[3];
};

#define S_IRUSR 0400
#define S_IWUSR 0200
#define S_IXUSR 0100
#define S_IRGRP 0040
#define S_IWGRP 0020
#define S_IXGRP 0010
#define S_IROTH 0004
#define S_IWOTH 0002
#define S_IXOTH 0001
#define S_ISREG(m) (((m) & 0170000) == 0100000)
#define S_ISDIR(m) (((m) & 0170000) == 0040000)
#define S_ISLNK(m) (((m) & 0170000) == 0120000)

extern int stat(const char *path, struct stat *buf);
extern int fstat(int fd, struct stat *buf);
extern int lstat(const char *path, struct stat *buf);
extern int fchmod(int fd, mode_t mode);
extern int mkdir(const char *path, mode_t mode);

#endif
