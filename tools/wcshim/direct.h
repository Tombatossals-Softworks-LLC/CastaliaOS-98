/* direct.h - Open Watcom stand-in. See README.md. */
#ifndef CASTALIA_WCSHIM_DIRECT_H
#define CASTALIA_WCSHIM_DIRECT_H
int   chdir(const char *path);
char *getcwd(char *buf, int size);
int   mkdir(const char *path);
int   rmdir(const char *path);
#endif
