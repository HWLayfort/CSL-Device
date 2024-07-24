#include <linux/list.h>
#include <linux/fs.h>
#include <linux/types.h>
#include <linux/xarray.h>
#include "type.h"

#ifndef __CSL_METADATA_OPS
#define __CSL_METADATA_OPS

#define PTR_TO_UINT64(p) ((uintptr_t)(p))
#define UINT64_TO_PTR(u) ((void *)(u))

#define CSL_SECTOR_SHIFT 9
#define CSL_SECTOR_SIZE (1 << CSL_SECTOR_SHIFT)
#define TOTAL_SECTORS 32768

#define IDX_PTR(dev, x) (void *)(dev->data + (x << CSL_SECTOR_SHIFT))

#define LIST_ENTRY_INIT(entry, __idx) \
    entry->idx = __idx;

#define MAPPING_ENTRY_INIT(entry, __l_idx, __p_idx) \
    entry->l_idx = __l_idx; \
    entry->p_idx = __p_idx;

int load_ptr(struct file *file, void **ptr);
int save_ptr(struct file *file, void *ptr);
int load_list(struct file *file, struct list_head *list);
int save_list(struct file *file, struct list_head *list);
int load_xa(struct file *file, struct xarray *xa);
int save_xa(struct file *file, struct xarray *xa);
#endif