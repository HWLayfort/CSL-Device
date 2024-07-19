#include "metadata.h"

int load_ptr(struct file *file, void **ptr) {
    size_t ret = 0;
    uintptr_t tmp = 0;
        
    ret = kernel_read(file, &tmp, sizeof(uintptr_t), &file->f_pos);
    if (ret <= 0) {
        return -ENOMEM;
    }
    *ptr = UINT64_TO_PTR(tmp);
    
    return 0;
}

int save_ptr(struct file *file, void *ptr) {
    uintptr_t tmp = PTR_TO_UINT64(ptr);
    kernel_write(file, &tmp, sizeof(uintptr_t), &file->f_pos);
    
    return 0;
}

int load_list(struct file *file, struct list_head *list) {
    size_t ret = 0;
        
    while(1) {
        int tmp;
        ret = kernel_read(file, &tmp, sizeof(tmp), &file->f_pos);
        if (ret <= 0) {
            break;
        }
        struct sector_list_entry *entry = (struct sector_list_entry *)kmalloc(sizeof(struct sector_list_entry), GFP_KERNEL);
        LIST_ENTRY_INIT(entry, tmp);
        list_add_tail(&entry->list, list);
    }
    
    return 0;
}

int save_list(struct file *file, struct list_head *list) {
    struct list_head *pos, *q;

    list_for_each_safe(pos, q, list) {
        struct sector_list_entry *item = list_entry(pos, struct sector_list_entry, list);
        kernel_write(file, &item->idx, sizeof(int), &file->f_pos);
        list_del(pos);
        kfree(item);
    }

    return 0;
}

int load_xa(struct file *file, struct xarray *xa) {
    size_t ret = 0;
        
    while(1) {
        int l_idx = 0;
        int p_idx = 0;
        ret = kernel_read(file, &l_idx, sizeof(int), &file->f_pos);
        ret = kernel_read(file, &p_idx, sizeof(int), &file->f_pos);
        if (ret <= 0) {
            break;
        }
        struct sector_mapping_entry *entry = (struct sector_mapping_entry *)kmalloc(sizeof(struct sector_mapping_entry), GFP_KERNEL);
        MAPPING_ENTRY_INIT(entry, l_idx, p_idx);
        xa_store(xa, l_idx, entry, GFP_KERNEL);
    }
    
    return 0;
}

int save_xa(struct file *file, struct xarray *xa) {
    unsigned long idx;
    struct sector_mapping_entry *data;
    
    xa_for_each(xa, idx, data) {
        kernel_write(file, &(data->l_idx), sizeof(int), &file->f_pos);
        kernel_write(file, &(data->p_idx), sizeof(int), &file->f_pos);
        kfree(data);
    }

    xa_destroy(xa);

    return 0;
}