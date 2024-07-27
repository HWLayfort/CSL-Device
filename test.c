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

void print_test_result(const char *test_name, int success) {
    if (success) {
        printf("\033[0;32m[✔] %s passed\033[0m\n", test_name);
    } else {
        printf("\033[0;31m[✘] %s failed\033[0m\n", test_name);
    }
}

void single_thread_write_test(int fd) {
    char *write_buffer;

    if (posix_memalign((void **)&write_buffer, SECTOR_SIZE, SECTOR_SIZE)) {
        perror("posix_memalign");
        close(fd);
        exit(EXIT_FAILURE);
    }

    memset(write_buffer, 0xAA, SECTOR_SIZE);

    int success = 1;
    for (int i = 0; i < NUM_SECTORS; i++) {
        off_t offset = i * SECTOR_SIZE;
        write_data(fd, offset, write_buffer, SECTOR_SIZE);
    }

    print_test_result("Single thread write test", success);
    free(write_buffer);
}

void single_thread_read_test(int fd) {
    char *write_buffer;
    char *read_buffer;

    if (posix_memalign((void **)&write_buffer, SECTOR_SIZE, SECTOR_SIZE)) {
        perror("posix_memalign");
        close(fd);
        exit(EXIT_FAILURE);
    }

    if (posix_memalign((void **)&read_buffer, SECTOR_SIZE, SECTOR_SIZE)) {
        perror("posix_memalign");
        free(write_buffer);
        close(fd);
        exit(EXIT_FAILURE);
    }

    memset(write_buffer, 0xAA, SECTOR_SIZE);

    int success = 1;
    for (int i = 0; i < NUM_SECTORS; i++) {
        off_t offset = i * SECTOR_SIZE;
        memset(read_buffer, 0, SECTOR_SIZE);
        read_data(fd, offset, read_buffer, SECTOR_SIZE);

        if (memcmp(write_buffer, read_buffer, SECTOR_SIZE) != 0) {
            success = 0;
            fprintf(stderr, "Data verification failed at offset %ld in single thread\n", offset);
            break;
        }
    }

    print_test_result("Single thread read test", success);
    free(write_buffer);
    free(read_buffer);
}

void *multithread_test_device(void *threadarg) {
    thread_data_t *data = (thread_data_t *)threadarg;
    int fd = data->fd;
    int thread_id = data->thread_id;
    pthread_mutex_t *mutex = data->mutex;

    char *write_buffer;
    char *read_buffer;

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

    memset(write_buffer, 0xAA + thread_id, SECTOR_SIZE);

    int success = 1;
    for (int i = 0; i < NUM_SECTORS; i++) {
        off_t offset = i * SECTOR_SIZE;

        pthread_mutex_lock(mutex);

        write_data(fd, offset, write_buffer, SECTOR_SIZE);

        memset(read_buffer, 0, SECTOR_SIZE);
        read_data(fd, offset, read_buffer, SECTOR_SIZE);

        if (memcmp(write_buffer, read_buffer, SECTOR_SIZE) != 0) {
            success = 0;
            fprintf(stderr, "Data verification failed at offset %ld in thread %d\n", offset, thread_id);
            break;
        }

        pthread_mutex_unlock(mutex);
    }

    char test_name[50];
    snprintf(test_name, sizeof(test_name), "Multithread test in thread %d", thread_id);
    print_test_result(test_name, success);

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

    single_thread_write_test(fd);
    single_thread_read_test(fd);

    for (int i = 0; i < NUM_THREADS; i++) {
        thread_data[i].thread_id = i;
        thread_data[i].fd = fd;
        thread_data[i].mutex = &mutex;
        int rc = pthread_create(&threads[i], NULL, multithread_test_device, (void *)&thread_data[i]);
        if (rc) {
            fprintf(stderr, "Error: Unable to create thread %d, %d\n", i, rc);
            close(fd);
            exit(EXIT_FAILURE);
        }
    }

    for (int i = 0; i < NUM_THREADS; i++) {
        pthread_join(threads[i], NULL);
    }

    close(fd);
    return 0;
}
