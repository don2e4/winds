#ifndef winds_unistd_h
#define winds_unistd_h

#include <sys/types.h>
#include <stddef.h>

#define SEEK_SET 0
#define SEEK_CUR 1
#define SEEK_END 2
#define F_OK 0
#define R_OK 4
#define W_OK 2
#define _SC_PAGESIZE 30

extern ssize_t read(int fd, void *buffer, size_t count);
extern ssize_t write(int fd, const void *buffer, size_t count);
extern int close(int fd);
extern off_t lseek(int fd, off_t offset, int whence);
extern int access(const char *path, int mode);
extern int unlink(const char *path);
extern int fsync(int fd);
extern int ftruncate(int fd, off_t length);
extern ssize_t pread(int fd, void *buffer, size_t count, off_t offset);
extern ssize_t pwrite(int fd, const void *buffer, size_t count, off_t offset);
extern uid_t geteuid(void);
extern int fchown(int fd, uid_t owner, gid_t group);
extern int chown(const char *path, uid_t owner, gid_t group);
extern ssize_t readlink(const char *path, char *buffer, size_t size);
extern int getpid(void);
extern char *getcwd(char *buffer, size_t size);
extern int rmdir(const char *path);
extern long sysconf(int name);

#endif
