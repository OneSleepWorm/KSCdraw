#ifndef __LITTLEFS_FS_H__
#define __LITTLEFS_FS_H__

#include "lfs.h"

typedef struct {
    lfs_file_t* file;
    const char* path;
    int         flags;
} lfs_file_op_t;

typedef struct {
    lfs_file_t* file;
    void*       buffer;
    lfs_size_t  size;
} lfs_rw_t;

typedef struct {
    lfs_file_t* file;
    lfs_soff_t  offset;
    int         whence;
} lfs_seek_t;

typedef struct {
    const char*      oldpath;
    const char*      newpath;
} lfs_rename_t;

typedef struct {
    const char*       path;
    struct lfs_info* info;
} lfs_stat_t;

typedef struct {
    lfs_dir_t*  dir;
    const char* path;
} lfs_dir_op_t;

typedef struct {
    lfs_dir_t*      dir;
    struct lfs_info* info;
} lfs_dir_read_t;

#endif
