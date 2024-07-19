#include <linux/fs.h>

#ifndef __CSL_FILE_OPS
#define __CSL_FILE_OPS

/**
 * __file_open - Open file with specified flag
 */
#define __file_open(path, flag) filp_open(path, flag, S_IRWXU)
#define file_open_read(path)    __file_open(path, O_RDONLY);
#define file_open(path)         __file_open(path, O_RDWR | O_TRUNC);
#define file_create(path)       __file_open(path, O_RDWR | O_CREAT | O_TRUNC);
#define file_close(file)        filp_close(file, NULL);

#endif