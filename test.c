#define _GNU_SOURCE

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <pthread.h>

#define DEVICE_PATH "/dev/csl"
#define SECTOR_SIZE 512
#define NUM_THREADS 4
#define NUM_SECTORS 10

typedef struct {
    int thread_id;
    int fd;
    pthread_mutex_t *mutex;
} thread_data_t;

void write_data(int fd, off_t offset, const void *data, size_t size) {
    if (lseek(fd, offset, SEEK_SET) == (off_t)-1) {
        perror("lseek");
        exit(EXIT_FAILURE);
    }
    if (write(fd, data, size) != size) {
        perror("write");
        exit(EXIT_FAILURE);
    }

    fsync(fd);
}

void read_data(int fd, off_t offset, void *data, size_t size) {
    if (lseek(fd, offset, SEEK_SET) == (off_t)-1) {
        perror("lseek");
        exit(EXIT_FAILURE);
    }
    if (read(fd, data, size) != size) {
        perror("read");
        exit(EXIT_FAILURE);
    }

    fsync(fd);
}

void *test_device(void *threadarg) {
    thread_data_t *data = (thread_data_t *)threadarg;
    int fd = data->fd;
    int thread_id = data->thread_id;
    pthread_mutex_t *mutex = data->mutex;

    // Allocate buffers for write and read
    char *write_buffer;
    char *read_buffer;

    // posix_memalign to ensure alignment for O_DIRECT
    if (posix_memalign((void **)&write_buffer, SECTOR_SIZE, SECTOR_SIZE)) {
        perror("posix_memalign");
        close(fd);
        pthread_exit(NULL);
    }

    if (posix_memalign((void **)&read_buffer, SECTOR_SIZE, SECTOR_SIZE)) {
        perror("posix_memalign");
        free(write_buffer);
        close(fd);
        pthread_exit(NULL);
    }

    // Fill write buffer with test data
    memset(write_buffer, 0xAA + thread_id, SECTOR_SIZE);

    // Write and read data with synchronization
    for (int i = 0; i < NUM_SECTORS; i++) {
        off_t offset = i * SECTOR_SIZE;

        // Lock the mutex before entering the critical section
        pthread_mutex_lock(mutex);

        // Write data to device
        write_data(fd, offset, write_buffer, SECTOR_SIZE);

        // Read data back
        memset(read_buffer, 0, SECTOR_SIZE);
        read_data(fd, offset, read_buffer, SECTOR_SIZE);

        // Verify data
        if (memcmp(write_buffer, read_buffer, SECTOR_SIZE) != 0) {
            fprintf(stderr, "Data verification failed at offset %ld in thread %d\n", offset, thread_id);
            free(write_buffer);
            free(read_buffer);
            close(fd);
            pthread_mutex_unlock(mutex);  // Unlock the mutex before exiting
            pthread_exit(NULL);
        }

        // Unlock the mutex after exiting the critical section
        pthread_mutex_unlock(mutex);
    }

    printf("Thread %d: All tests passed successfully!\n", thread_id);

    // Free allocated memory and close file descriptor
    free(write_buffer);
    free(read_buffer);

    pthread_exit(NULL);
}

int main() {
    pthread_t threads[NUM_THREADS];
    thread_data_t thread_data[NUM_THREADS];
    pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
    int fd = open(DEVICE_PATH, O_RDWR | O_DIRECT);

    if (fd == -1) {
        perror("open");
        exit(EXIT_FAILURE);
    }

    // Create threads
    for (int i = 0; i < NUM_THREADS; i++) {
        thread_data[i].thread_id = i;
        thread_data[i].fd = fd;
        thread_data[i].mutex = &mutex;
        int rc = pthread_create(&threads[i], NULL, test_device, (void *)&thread_data[i]);
        if (rc) {
            fprintf(stderr, "Error: Unable to create thread %d, %d\n", i, rc);
            close(fd);
            exit(EXIT_FAILURE);
        }
    }

    // Wait for all threads to complete
    for (int i = 0; i < NUM_THREADS; i++) {
        pthread_join(threads[i], NULL);
    }

    close(fd);
    return 0;
}
