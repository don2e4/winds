#ifndef winds_errno_h
#define winds_errno_h

extern int *__errno_location(void);
#define errno (*__errno_location())
#define EAGAIN 11
#define EWOULDBLOCK EAGAIN
#define EPERM 1
#define ENOENT 2
#define EINTR 4
#define EIO 5
#define EACCES 13
#define EBUSY 16
#define EEXIST 17
#define EISDIR 21
#define ENOSPC 28
#define ERANGE 34
#define ENOLCK 37
#define ETIMEDOUT 110

#endif
