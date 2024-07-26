#include "metadata.h"

/**
 * load_ptr - Load a data buffer address from a file
 *
 * @file: File pointer that contains the data buffer address
 * @ptr: Pointer to the data buffer address
 *
 * Return: 0 on success, -ENOMEM on failure
 */
int load_ptr(struct file* file, void** ptr) {
	size_t ret = 0;
	uintptr_t tmp = 0;

	ret = kernel_read(file, &tmp, sizeof(uintptr_t), &file->f_pos);
	if (ret <= 0) {
		return -ENOMEM;
	}
	*ptr = UINT64_TO_PTR(tmp);

	return 0;
}

/**
 * save_ptr - Save a data buffer address to a file
 *
 * @file: File pointer to save the data buffer address
 * @ptr: Pointer to the data buffer address
 */
int save_ptr(struct file* file, void* ptr) {
	uintptr_t tmp = PTR_TO_UINT64(ptr);
	kernel_write(file, &tmp, sizeof(uintptr_t), &file->f_pos);

	return 0;
}

/**
 * load_list - Load a list from a file
 *
 * @file: File pointer that contains the list
 * @list: List to load
 */
int load_list(struct file* file, struct list_head* list) {
	size_t ret = 0;

	while (1) {
		int tmp;
		ret = kernel_read(file, &tmp, sizeof(tmp), &file->f_pos);
		if (ret <= 0) {
			break;
		}
		struct sector_list_entry* entry =
		    (struct sector_list_entry*)kmalloc(
			sizeof(struct sector_list_entry), GFP_KERNEL);
		LIST_ENTRY_INIT(entry, tmp);
		list_add_tail(&entry->list, list);
	}

	return 0;
}

/**
 * save_list - Save a list to a file
 *
 * @file: File pointer to save the list
 * @list: List to save
 */
int save_list(struct file* file, struct list_head* list) {
	struct list_head *pos, *q;

	list_for_each_safe(pos, q, list) {
		struct sector_list_entry* item =
		    list_entry(pos, struct sector_list_entry, list);
		kernel_write(file, &item->idx, sizeof(int), &file->f_pos);
		list_del(pos);
		kfree(item);
	}

	return 0;
}

/**
 * load_xa - Load an xarray from a file
 *
 * @file: File pointer that contains the xarray
 * @xa: xarray to load
 */
int load_xa(struct file* file, struct xarray* xa) {
	size_t ret = 0;

	while (1) {
		int l_idx = 0;
		int p_idx = 0;
		ret = kernel_read(file, &l_idx, sizeof(int), &file->f_pos);
		ret = kernel_read(file, &p_idx, sizeof(int), &file->f_pos);
		if (ret <= 0) {
			break;
		}
		struct sector_mapping_entry* entry =
		    (struct sector_mapping_entry*)kmalloc(
			sizeof(struct sector_mapping_entry), GFP_KERNEL);
		MAPPING_ENTRY_INIT(entry, l_idx, p_idx);
		xa_store(xa, l_idx, entry, GFP_KERNEL);
	}

	return 0;
}

/**
 * save_xa - Save an xarray to a file
 *
 * @file: File pointer to save the xarray
 * @xa: xarray to save
 */
int save_xa(struct file* file, struct xarray* xa) {
	unsigned long idx;
	struct sector_mapping_entry* data;

	xa_for_each(xa, idx, data) {
		kernel_write(file, &(data->l_idx), sizeof(int), &file->f_pos);
		kernel_write(file, &(data->p_idx), sizeof(int), &file->f_pos);
		kfree(data);
	}

	xa_destroy(xa);

	return 0;
}

/**
 * initialize_memory - Initialize memory buffer
 *
 * @dev: Device pointer
 *
 * Create a metadata file to store the memory buffer and allocate the memory
 * buffer
 *
 * Return: 0 on success, -ENOMEM on failure
 */
int initialize_memory(struct csl_device* dev) {
	struct file* file = file_create(PATH);

	if (IS_ERR(file)) {
		pr_err("%sFailed to create metadata file. Errorcode: %ld\n",
		       PROMPT, PTR_ERR(file));
		return PTR_ERR(file);
	}

	file_close(file);

	dev->data = vmalloc(dev->size);

	if (!dev->data) {
		pr_err("%sFailed to allocate data buffer\n", PROMPT);
		return -ENOMEM;
	}

	DEBUG_MESSAGE("%sMemory initialized\n", PROMPT);

	return 0;
}

/**
 * initialize_metadata - Initialize metadata
 *
 * @dev: Device pointer
 *
 * Create metadata files to store the freelist, dirtylist, and mapping table
 * If data buffer is not allocated, initialize the memory buffer
 *
 * Return: 0 on success, -1 on failure
 */
int initialize_metadata(struct csl_device* dev) {
	if (!dev->data)
		initialize_memory(dev);

	struct file* freefile = file_create(FREELIST_PATH);
	struct file* dirtyfile = file_create(DIRTYLIST_PATH);
	struct file* mapfile = file_create(MAP_PATH);

	if (!freefile || !dirtyfile || !mapfile) {
		pr_err("%sFailed to create metadata files\n", PROMPT);
		return -1;
	}

	file_close(freefile);
	file_close(dirtyfile);
	file_close(mapfile);

	for (int i = 0; i < TOTAL_SECTORS; i++) {
		struct sector_list_entry* item =
		    (struct sector_list_entry*)kmalloc(
			sizeof(struct sector_list_entry), GFP_KERNEL);
		item->idx = i;
		list_add_tail(&item->list, &dev->freelist);
	}

	DEBUG_MESSAGE("%sMetadata initialized\n", PROMPT);

	return 0;
}

/**
 * load_metadata - Load metadata
 *
 * @dev: Device pointer
 * @reset_device: Flag to reset the device
 *
 * Load the metadata from the metadata files
 * If the metadata files do not exist, initialize the metadata
 *
 * Return: 0 on success, -1 on failure
 */
int load_metadata(struct csl_device* dev, int reset_device) {
	struct file* file = file_open_read(PATH);
	if (IS_ERR(file)) {
		if (PTR_ERR(file) == -ENOENT) {
			pr_info("%sMemory file not exist\n", PROMPT);
			goto initialize_memory;
		} else {
			pr_err("%sMemory file crushed. Initialize Memory.\n",
			       PROMPT);
			goto initialize_memory;
		}
	}

	load_ptr(file, (void**)&dev->data);
	file_close(file);

	if (reset_device) {
		pr_info("%sReset device\n", PROMPT);
		vfree(dev->data);
		goto initialize_memory;
	}

	struct file* freefile = NULL;
	struct file* dirtyfile = NULL;
	struct file* mapfile = NULL;

	freefile = file_open_read(FREELIST_PATH);
	dirtyfile = file_open_read(DIRTYLIST_PATH);
	mapfile = file_open_read(MAP_PATH);

	if (IS_ERR(freefile) || IS_ERR(dirtyfile) || IS_ERR(mapfile)) {
		if ((PTR_ERR(freefile) == -ENOENT)
		    && (PTR_ERR(dirtyfile) == -ENOENT)
		    && (PTR_ERR(mapfile) == -ENOENT)) {
			pr_info("%sMetadata file not exist\n", PROMPT);
			goto initialize_metadata;
		} else {
			pr_err(
			    "%sMetadata file crushed. Initialize Metadata.\n",
			    PROMPT);
			goto initialize_metadata;
		}
	}

	load_list(freefile, &dev->freelist);
	load_list(dirtyfile, &dev->dirtylist);
	load_xa(mapfile, &dev->map);

	file_close(freefile);
	file_close(dirtyfile);
	file_close(mapfile);

	DEBUG_MESSAGE("%sMetadata loaded\n", PROMPT);

	return 0;

initialize_memory:
	initialize_memory(dev);

initialize_metadata:
	return initialize_metadata(dev);
}

/**
 * save_metadata - Save metadata
 *
 * @dev: Device pointer
 *
 * Save the metadata to the metadata files
 */
void save_metadata(struct csl_device* dev) {
	struct file* file = file_open(PATH);
	struct file* freefile = file_open(FREELIST_PATH);
	struct file* dirtyfile = file_open(DIRTYLIST_PATH);
	struct file* mapfile = file_open(MAP_PATH);

	save_ptr(file, (void*)dev->data);
	save_list(freefile, &dev->freelist);
	save_list(dirtyfile, &dev->dirtylist);
	save_xa(mapfile, &dev->map);

	file_close(file);
	file_close(freefile);
	file_close(dirtyfile);
	file_close(mapfile);

	DEBUG_MESSAGE("%sMetadata saved\n", PROMPT);
}