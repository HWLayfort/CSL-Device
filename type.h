#include <linux/blk-mq.h>
#include <linux/blkdev.h>
#include <linux/list.h>
#include <linux/mutex.h>
#include <linux/rwlock.h>
#include <linux/semaphore.h>
#include <linux/types.h>
#include <linux/xarray.h>
#include <linux/rwsem.h>

#ifndef __CSL_DEV_TYPES
#define __CSL_DEV_TYPES

/**
 * struct sector_list_entry - Sector list entry structure
 * @idx: 	Pysical sector index
 * @list: 	List head
 */
struct sector_list_entry {
	int idx;
	struct list_head list;
};

/**
 * struct sector_mapping_entry - Sector mapping entry structure
 * @l_idx: 	Logical sector index
 * @p_idx: 	Physical sector index
 */
struct sector_mapping_entry {
	int l_idx;
	int p_idx;
};

/**
 * struct csl_device - CSL append only ramdisk device structure
 * @tag_set: 				Tag set for multiqueue
 * @disk: 				General disk structure
 * @queue: 				Request queue
 * @reader_cnt_mutex: 			Mutex for reader count
 * 					- only for mutex and semaphore option
 * @rw_mutex: 				Mutex for read-write lock
 * 					- only for mutex and semaphore option
 * @reader_nr: 				Reader count
 * 					- only for mutex and semaphore option
 * @rwlock: 				Read-write lock
 * @map: 				Map for logical to physical sector index
 * @freelist: 				Free physical sector list
 * @dirtylist: 				Dirty physical sector list
 * @size: 				Device capacity in sectors
 * @data: 				Data buffer address
 */
struct csl_device {
	struct blk_mq_tag_set* tag_set; /* Tag set for multiqueue */
	struct gendisk* disk;		/* General disk structure */
	struct request_queue* queue;	/* Request queue */
#ifdef _USE_MUTEX
	struct mutex reader_cnt_mutex; /* Mutex for reader count */
	struct mutex rw_mutex;	     /* Mutex for read-write lock */
	size_t reader_nr;	     /* Read count */
#elif _USE_SEMAPHORE
	struct semaphore reader_cnt_mutex; /* Spinlock for reader count */
	struct semaphore rw_mutex;	   /* Semaphore for read-write lock */
	size_t reader_nr;		   /* Read count */
#elif _USE_RWSEMAPHORE
	struct rw_semaphore rw_mutex; /* Read-write semaphore */
#else
	rwlock_t rwlock; /* Read-write lock */
#endif
	struct xarray map;	    /* Map for block index */
	struct list_head freelist;  /* Free block list */
	struct list_head dirtylist; /* Dirty block list */
	size_t size;		    /* Device capacity in sectors */
	uint8_t* data;		    /* Data buffer */
};
#endif