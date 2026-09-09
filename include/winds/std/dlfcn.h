#ifndef winds_dlfcn_h
#define winds_dlfcn_h

#define RTLD_NOW 2
#define RTLD_GLOBAL 256

extern void *dlopen(const char *file, int mode);
extern void *dlsym(void *handle, const char *name);
extern int dlclose(void *handle);
extern char *dlerror(void);

#endif
