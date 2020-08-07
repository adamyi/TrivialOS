#pragma once

#include <sys/types.h>
#include "../coroutine/picoro.h"

#define OPEN_MAX 128

#define MIN_FD 0

#define IS_VALID_FD(fd) (((fd) >= (0)) && ((fd) < (OPEN_MAX)))

typedef struct fdesc {
  struct vnode *vnode;
  int flag;

  off_t offset;
  int refcount;

} fdesc_t;

int fdesc_open(char *filename, int flags, mode_t mode, fdesc_t** result, coro_t me);
void fdesc_increment(fdesc_t* fd, coro_t me);
void fdesc_decrement(fdesc_t* fd, coro_t me);
void fdesc_destroy(fdesc_t* fd, coro_t me);

typedef struct fdtable {
  fdesc_t *fds[OPEN_MAX];
} fdtable_t;

void fdtable_init(fdtable_t *fdt, coro_t me);
void fdtable_destroy(fdtable_t *ft, coro_t me);
int fdtable_get(fdtable_t *ft, int fd, fdesc_t **result, coro_t me);
int fdtable_append(fdtable_t *ft, fdesc_t *file, coro_t me);
int fdtable_put(fdtable_t *ft, int fd, fdesc_t *file, coro_t me);

