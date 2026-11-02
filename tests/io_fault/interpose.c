// Linux test-only syscall interposer. Match only the requested temporary file.
#define _GNU_SOURCE
#include <dlfcn.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/uio.h>
#include <unistd.h>

static int selected(int fd, const char *op) {
    char proc[64], path[4096];
    const char *prefix=getenv("SDB_FAULT_PREFIX"), *mode=getenv("SDB_FAULT_MODE");
    if (!prefix || !*prefix || !mode || strcmp(mode,op)) return 0;
    snprintf(proc,sizeof proc,"/proc/self/fd/%d",fd);
    ssize_t n=readlink(proc,path,sizeof(path)-1);
    if(n<0) return 0;
    path[n]=0;
    return strncmp(path,prefix,strlen(prefix))==0;
}
ssize_t write(int fd,const void *buf,size_t n) {
    if(selected(fd,"full")) {errno=ENOSPC;return -1;}
    ssize_t (*real)(int,const void*,size_t)=dlsym(RTLD_NEXT,"write");
    return real(fd,buf,n);
}
ssize_t writev(int fd,const struct iovec *vec,int n) {
    if(selected(fd,"full")) {errno=ENOSPC;return -1;}
    ssize_t (*real)(int,const struct iovec*,int)=dlsym(RTLD_NEXT,"writev");
    return real(fd,vec,n);
}
int fsync(int fd) {
    if(selected(fd,"io")) {errno=EIO;return -1;}
    int (*real)(int)=dlsym(RTLD_NEXT,"fsync");
    return real(fd);
}
