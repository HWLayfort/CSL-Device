#include <linux/blk-mq.h>
#include <linux/blkdev.h>
#include <linux/delay.h>
#include <linux/hdreg.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/version.h>
#include <linux/vmalloc.h>
#include <linux/xarray.h>
#include <linux/spinlock.h>

#include "file.h"
#include "metadata.h"
#include "type.h"

/* if DEBUG is enabled, print debug message */
#ifdef _USE_MUTEX
#define GET_READ_LOCK(dev)                  \
	mutex_lock(&dev->reader_cnt_mutex); \
	dev->reader_nr++;                   \
	if (dev->reader_nr == 1)            \
		mutex_lock(&dev->rw_mutex); \
	mutex_unlock(&dev->reader_cnt_mutex);
#define RELEASE_READ_LOCK(dev)                \
	mutex_lock(&dev->reader_cnt_mutex);    \
	dev->reader_nr--;                     \
	if (dev->reader_nr == 0)              \
		mutex_unlock(&dev->rw_mutex); \
	mutex_unlock(&dev->reader_cnt_mutex);
#define GET_WRITE_LOCK(dev) mutex_lock(&dev->rw_mutex);
#define RELEASE_WRITE_LOCK(dev) mutex_unlock(&dev->rw_mutex);
#elif _USE_SEMAPHORE
#define GET_READ_LOCK(dev)                 \
	down(&dev->reader_cnt_mutex);      \
	dev->reader_nr++;                  \
	if (dev->reader_nr == 1)           \
		down(&dev->rw_mutex);      \
	up(&dev->reader_cnt_mutex);
#define RELEASE_READ_LOCK(dev)             \
	down(&dev->reader_cnt_mutex);      \
	dev->reader_nr--;                  \
	if (dev->reader_nr == 0)           \
		up(&dev->rw_mutex);        \
	up(&dev->reader_cnt_mutex);
#define GET_WRITE_LOCK(dev) down(&dev->rw_mutex);
#define RELEASE_WRITE_LOCK(dev) up(&dev->rw_mutex);
#elif _USE_RWSEMAPHORE
#define GET_READ_LOCK(dev) down_read(&dev->rw_mutex);
#define RELEASE_READ_LOCK(dev) up_read(&dev->rw_mutex);
#define GET_WRITE_LOCK(dev) down_write(&dev->rw_mutex);
#define RELEASE_WRITE_LOCK(dev) up_write(&dev->rw_mutex);
#else
#define GET_READ_LOCK(dev) read_lock(&dev->rwlock);
#define RELEASE_READ_LOCK(dev) read_unlock(&dev->rwlock);
#define GET_WRITE_LOCK(dev) write_lock(&dev->rwlock);
#define RELEASE_WRITE_LOCK(dev) write_unlock(&dev->rwlock);
#endif

/* Module information */
MODULE_LICENSE("GPL");
MODULE_AUTHOR("Bae Mun Sung");
MODULE_DESCRIPTION("Simple block device driver");

static uint __reset_device = 0;

module_param(__reset_device, uint, S_IRUGO);

MODULE_PARM_DESC(__reset_device, "Reset device");
/* Device major number */
static int dev_major = 0;

/* Pointer to the device structure */
static struct csl_device* dev = NULL;


/* Function to open the block device */
static int dev_open(struct gendisk* disk, blk_mode_t mode) {
	pr_info("%sdevice open\n", PROMPT);
	if (!blk_get_queue(disk->queue)) {
		pr_err("%sFailed to get queue\n", PROMPT);
		return -ENXIO;
	}

	return 0;
}

/* Function to release the block device */
static void dev_release(struct gendisk* disk) {
	pr_info("%sdevice release\n", PROMPT);
	blk_put_queue(disk->queue);
}

/* Block device operations structure */
static struct block_device_operations csl_dev_ops = {
    .owner = THIS_MODULE, .open = dev_open, .release = dev_release};

/**
 * garbage_collecting - Garbage collecting
 *
 * @dev: Device pointer
 *
 * Move all dirty blocks to the free list
 */
static void garbage_collecting(struct csl_device* dev) {
	DEBUG_MESSAGE("%sFree list is empty. Garbage collecting\n", PROMPT);

	struct sector_list_entry *tmp, *n;

	list_for_each_entry_safe(tmp, n, &dev->dirtylist, list) {
		list_del(&tmp->list);
		list_add_tail(&tmp->list, &dev->freelist);
	}
}

/**
 * read_sector - Read sector from device
 *
 * @dev: Device pointer
 * @idx: Sector index
 * @buf: Buffer to store data
 * @len: Length of data
 *
 * Read data from the device and store it in the buffer
 */
static void read_sector(struct csl_device* dev, unsigned long idx, void* buf,
			unsigned int len) {
	void* ret;
	struct sector_mapping_entry* entry;

	GET_READ_LOCK(dev);

	entry = xa_load(&dev->map, idx);

	if (!entry) {
		DEBUG_MESSAGE("%sBlock not found in map\n", PROMPT);
	} else {
		ret = IDX_PTR(dev, entry->p_idx);
		memcpy(buf, ret, len);
	}

	RELEASE_READ_LOCK(dev);
}

/**
 * write_sector - Write sector to device
 *
 * @dev: Device pointer
 * @idx: Sector index
 * @buf: Buffer to store data
 * @len: Length of data
 *
 * Write data to the device from the buffer
 */
static void write_sector(struct csl_device* dev, unsigned long idx, void* buf,
			 unsigned int len) {
	void* ret;
	struct sector_mapping_entry* entry;

	GET_WRITE_LOCK(dev);

	entry = xa_load(&dev->map, idx);

	/**
	 * If the block is not found in the map, find a free block from free
	 * list if the free list is empty, run garbage collecting. then, insert
	 * the block into the map
	 *
	 * If the block is found in the map, insert it into the dirty list and
	 * find a free block from free list and exchange it with the block.
	 * if the free list is empty, run garbage collecting.
	 */
	if (!entry) {
		DEBUG_MESSAGE("%sBlock not found in map\n", PROMPT);

		if (list_empty(&dev->freelist))
			garbage_collecting(dev);

		/* find free block */
		struct sector_list_entry* free_block = list_first_entry(
		    &dev->freelist, struct sector_list_entry, list);
		list_del(&free_block->list);
		ret = IDX_PTR(dev, free_block->idx);

		/* insert block into map */
		entry = (struct sector_mapping_entry*)kmalloc(
		    sizeof(struct sector_mapping_entry), GFP_KERNEL);
		MAPPING_ENTRY_INIT(entry, idx, free_block->idx);

		kfree(free_block);

		void* store_ret = xa_store(&dev->map, idx, entry, GFP_KERNEL);
		if (xa_is_err(store_ret)) {
			pr_err("%sFailed to insert block "
			       "into map. Errorcode:%d\n",
			       PROMPT, xa_err(store_ret));
			RELEASE_WRITE_LOCK(dev);
			return;
		}
	} else {
		DEBUG_MESSAGE("%sBlock found in map\n", PROMPT);

		/* insert block into dirty list */
		struct sector_list_entry* dirty_block =
		    (struct sector_list_entry*)kmalloc(
			sizeof(struct sector_list_entry), GFP_KERNEL);
		LIST_ENTRY_INIT(dirty_block, entry->p_idx);
		list_add_tail(&dirty_block->list, &dev->dirtylist);

		if (list_empty(&dev->freelist))
			garbage_collecting(dev);

		/* find free block */
		struct sector_list_entry* free_block = list_first_entry(
		    &dev->freelist, struct sector_list_entry, list);
		list_del(&free_block->list);
		ret = IDX_PTR(dev, free_block->idx);

		/* exchange block into map */
		struct sector_mapping_entry* new_entry =
		    (struct sector_mapping_entry*)kmalloc(
			sizeof(struct sector_mapping_entry), GFP_KERNEL);
		MAPPING_ENTRY_INIT(new_entry, idx, free_block->idx);

		kfree(free_block);

		void* cmpxchg_ret =
		    xa_cmpxchg(&dev->map, idx, entry, new_entry, GFP_KERNEL);
		if (xa_is_err(cmpxchg_ret)) {
			pr_err("%sFailed to exchange "
			       "block into map. "
			       "Errorcode:%d\n",
			       PROMPT, xa_err(cmpxchg_ret));
			RELEASE_WRITE_LOCK(dev);
			return;
		}
	}

	DEBUG_MESSAGE("%sBlock Index: %ld, Block Address: %p\n", PROMPT, idx,
		      ret);

	memcpy(ret, buf, len);
	RELEASE_WRITE_LOCK(dev);
}

/* Function to handle block requests */
static int dev_request_handle(struct request* rq, unsigned int* nr_bytes) {
	struct bio_vec bvec;
	struct req_iterator iter;
	struct csl_device* dev = rq->q->queuedata;
	loff_t pos = blk_rq_pos(rq) << CSL_SECTOR_SHIFT;
	loff_t dev_size = (loff_t)(dev->size);

	/* Iterate over each segment of the request */
	rq_for_each_segment(bvec, rq, iter) {
		unsigned long b_len = bvec.bv_len;
		void* b_buf = page_address(bvec.bv_page) + bvec.bv_offset;

		/* Ensure the request does not exceed the device
		 * size */
		if ((pos + b_len) > dev_size)
			b_len = (unsigned long)(dev_size - pos);

		unsigned long idx = pos >> CSL_SECTOR_SHIFT;

		DEBUG_MESSAGE("%sBlock length: %ld, Block "
			      "index: %ld, Request "
			      "direction: %s\n",
			      PROMPT, b_len, idx,
			      rq_data_dir(rq) == WRITE ? "WRITE" : "READ");

		/* Handle read or write request */
		if (rq_data_dir(rq) == WRITE)
			write_sector(dev, idx, b_buf, b_len);
		else
			read_sector(dev, idx, b_buf, b_len);

		if (IS_ENABLED(DEBUG))
			print_metadata(dev);

		pos += b_len;
		*nr_bytes += b_len;
	}

	return 0;
}

/* Function to process block requests */
static blk_status_t dev_request(struct blk_mq_hw_ctx* hctx,
				const struct blk_mq_queue_data* bd) {
	unsigned int nr_bytes = 0;
	blk_status_t status = BLK_STS_OK;
	struct request* rq = bd->rq;

	blk_mq_start_request(rq);

	if (dev_request_handle(rq, &nr_bytes) != 0)
		status = BLK_STS_IOERR;

	if (blk_update_request(rq, status, nr_bytes)) {
		pr_err("%sblk_update_request Failed", PROMPT);
		BUG();
	}

	blk_mq_end_request(rq, status);

	return status;
}

/* Block multiqueue operations structure */
static struct blk_mq_ops csl_dev_mq_ops = {
    .queue_rq = dev_request,
};

/* Initialize the csl driver */
static int __init csl_driver_init(void) {
	if (LINUX_VERSION_CODE < KERNEL_VERSION(6, 9, 5))
		pr_err("%sKernel version is too old\n", PROMPT);

	int status = 0;

	/* Register the block device */
	dev_major = register_blkdev(dev_major, DEVICE_NAME);
	if (dev_major < 0) {
		pr_err("%sFailed to register device with major "
		       "number %d\n",
		       PROMPT, dev_major);
		status = -EBUSY;
		goto dev_register_fail;
	}

	DEBUG_MESSAGE("%sDevice registered with major number %d\n", PROMPT,
		      dev_major);

	/* Allocate memory for the device structure */
	dev = kzalloc(sizeof(struct csl_device), GFP_KERNEL);
	if (!dev) {
		pr_err("%sFailed to allocate device structure\n", PROMPT);
		status = -ENOMEM;
		goto dev_allocation_fail;
	}

#ifdef _USE_MUTEX
	mutex_init(&dev->reader_cnt_mutex);
	mutex_init(&dev->rw_mutex);
	dev->reader_nr = 0;
	pr_info("%sUsing mutex\n", PROMPT);
#elif _USE_SEMAPHORE
	sema_init(&dev->reader_cnt_mutex, 1);
	sema_init(&dev->rw_mutex, 1);
	dev->reader_nr = 0;
	pr_info("%sUsing semaphore\n", PROMPT);
#elif _USE_RWSEMAPHORE
	init_rwsem(&dev->rw_mutex);
	pr_info("%sUsing rw_semaphore\n", PROMPT);
#else
	rwlock_init(&dev->rwlock);
	pr_info("%sUsing rwlock\n", PROMPT);
#endif
	xa_init(&dev->map);
	INIT_LIST_HEAD(&dev->freelist);
	INIT_LIST_HEAD(&dev->dirtylist);

	/* Set device capacity */
	dev->size = TOTAL_SECTORS << CSL_SECTOR_SHIFT;

	/* Allocate memory for the data buffer */
	if (load_metadata(dev, __reset_device) != 0) {
		pr_err("%sFailed to load metadata\n", PROMPT);
		status = -ENOMEM;
		goto dev_allocation_fail;
	}

	DEBUG_MESSAGE("%sdata adress: %p", PROMPT, dev->data);

	/* Allocate memory for the gendisk structure */
	dev->disk = blk_alloc_disk(NULL, NUMA_NO_NODE);
	if (dev->disk == NULL) {
		pr_err("%sFailed to allocate disk\n", PROMPT);
		status = -ENOMEM;
		goto disk_allocation_fail;
	}

	/* Allocate memory for the tag set */
	dev->tag_set = kzalloc(sizeof(struct blk_mq_tag_set), GFP_KERNEL);
	if (dev->tag_set == NULL) {
		pr_err("%sFailed to allocate tag set\n", PROMPT);
		status = -ENOMEM;
		goto disk_allocation_fail;
	}

	/* Initialize the tag set */
	dev->tag_set->ops = &csl_dev_mq_ops;
	dev->tag_set->nr_hw_queues = num_possible_cpus();
	dev->tag_set->queue_depth = 128;
	dev->tag_set->numa_node = NUMA_NO_NODE;
	dev->tag_set->cmd_size = 0;
	dev->tag_set->flags = BLK_MQ_F_SHOULD_MERGE;
	dev->tag_set->driver_data = dev;

	/* Allocate the tag set */
	status = blk_mq_alloc_tag_set(dev->tag_set);
	if (status) {
		pr_err("%sFailed to allocate tag set\n", PROMPT);
		goto tag_allocation_failed;
	}

	/* Initialize the request queue */
	dev->queue = dev->disk->queue;
	status = blk_mq_init_allocated_queue(dev->tag_set, dev->queue);
	if (status) {
		pr_err("%sFailed to allocate queues\n", PROMPT);
		goto queue_allocated_failed;
	}

	/* Set gendisk properties */
	dev->disk->major = dev_major;
	dev->disk->first_minor = 0;
	dev->disk->minors = 1;
	dev->disk->fops = &csl_dev_ops;
	dev->disk->flags = GENHD_FL_NO_PART;
	dev->disk->private_data = dev;
	dev->disk->queue->queuedata = dev;

	/* Set the device name */
	sprintf(dev->disk->disk_name, DEVICE_NAME);

	/* Set the capacity of the device */
	set_capacity(dev->disk, dev->size >> SECTOR_SHIFT);

	/* Set the logical block size */
	blk_queue_logical_block_size(dev->queue, CSL_SECTOR_SIZE);

	/* Add the disk to the system */
	status = add_disk(dev->disk);
	if (status) {
		pr_err("%sFailed to add disk\n", PROMPT);
		goto queue_allocated_failed;
	}

	DEBUG_MESSAGE("%scsl device driver init\n", PROMPT);

	return 0;

queue_allocated_failed:
	blk_mq_free_tag_set(dev->tag_set);

tag_allocation_failed:
	kfree(dev->tag_set);
	dev->tag_set = NULL;

disk_allocation_fail:
	vfree(dev->data);
	dev->data = NULL;
	kfree(dev);
	dev = NULL;

dev_allocation_fail:
	unregister_blkdev(dev_major, DEVICE_NAME);

dev_register_fail:
	pr_err("%scsl device driver failed with ERRORCODE %d\n", PROMPT,
	       status);
	return status;
}

/* Exit the csl driver */
static void __exit csl_driver_exit(void) {
	save_metadata(dev);
	del_gendisk(dev->disk);
	put_disk(dev->disk);
	blk_mq_free_tag_set(dev->tag_set);
	kfree(dev->tag_set);
	kfree(dev);
	unregister_blkdev(dev_major, DEVICE_NAME);
	pr_info("%scsl device driver exit\n", PROMPT);
}

module_init(csl_driver_init);
module_exit(csl_driver_exit);
