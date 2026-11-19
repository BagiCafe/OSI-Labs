#ifndef COMMON_H
#define COMMON_H
#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <signal.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <limits.h>
#include <ctype.h>

enum { OK, BAD_INPUT, DIV_ZERO, STOP, IO_ERROR };
typedef struct {
    int status;
    char text[];
} message_t;

static message_t *map_file(int fd, size_t *size) {
    struct stat st;
    if (fstat(fd, &st) == -1) { perror("fstat"); return NULL; }
    *size = (size_t)st.st_size;
    if (*size < sizeof(message_t)) return NULL;
    void *p = mmap(NULL, *size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (p == MAP_FAILED) { perror("mmap"); return NULL; }
    return p;
}

static int unmap_file(message_t *m, size_t size) {
    if (munmap(m, size) == -1) { perror("munmap"); return -1; }
    return 0;
}
#endif
