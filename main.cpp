
/*
TODO

-r: read
-w: write
-x: combined

-s: sequential
-r: random

multiple next buffers, in multithreaded fashion?
 */

#define _LARGEFILE64_SOURCE

// Linux API's
#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <linux/fs.h>
#include <unistd.h>
#include <fcntl.h>

// C standard library
#include <cstring>
#include <cassert>
#include <ctime>

// C++ standard library
#include <iostream>
#include <iomanip>
#include <sstream>
#include <vector>

// POSIX
#include <aio.h>

// Local
#include "progress.hpp"

enum class Mode {
    READ,
    WRITE
};

enum class Fill {
    ZERO,
    INDEX,
    HASH
};

static uint64_t hash64shift(uint64_t key) {
    key = (~key) + (key << 21); // key = (key << 21) - key - 1;
    key = key ^ (key >> 24);
    key = (key + (key << 3)) + (key << 8); // key * 265
    key = key ^ (key >> 14);
    key = (key + (key << 2)) + (key << 4); // key * 21
    key = key ^ (key >> 28);
    key = key + (key << 31);
    return key;
}

void fill(char *buffer, size_t bufferSize, uint64_t offset, Fill f) {
    // TODO: openmp?
    unsigned int chars_per_uint64 = sizeof(uint64_t) / sizeof(char);
    switch (f) {
    case Fill::ZERO:
        for (size_t index = 0; index < bufferSize; index += chars_per_uint64)
            *(uint64_t *)(buffer + index) = 0;
        break;
    case Fill::INDEX:
        for (size_t index = 0; index < bufferSize; index += chars_per_uint64)
            *(uint64_t *)(buffer + index) = offset + index;
        break;
    case Fill::HASH:
        for (size_t index = 0; index < bufferSize; index += chars_per_uint64)
            *(uint64_t *)(buffer + index) = hash64shift(offset + index);
        break;
    }
}

int main(int argc, char **argv) {
    // parse options
    Mode m = Mode::READ;
    Fill f = Fill::ZERO;
    int block_size = 4096;
    int blocks_at_once = 8192;
    int c;
    optarg = NULL;
    while ((c = getopt(argc, argv, "rwzihb:c:")) != -1) {
        switch (c) {
        // program modes
        case 'r':
            m = Mode::READ;
            break;
        case 'w':
            m = Mode::WRITE;
            break;
        // fill modes
        case 'z':
            f = Fill::ZERO;
            break;
        case 'i':
            f = Fill::INDEX;
            break;
        case 'h':
            f = Fill::HASH;
            break;
        // io parameters
        case 'b':
            block_size = atoi(optarg);
            break;
        case 'c':
            blocks_at_once = atoi(optarg);
            break;
        default:
            return -1;
            break;
        }
    }

    // parse positional options
    if (argc - optind != 1) {
        std::cerr << "No device specified" << std::endl;
        return 1;
    }
    char *device = argv[optind];
    std::cout << "Program mode: " << (m == Mode::READ ? "read" : "write")
              << std::endl;

    // calculate buffer sizes
    size_t buffer_size = block_size * blocks_at_once;
    if (buffer_size % sizeof(uint64_t) != 0) {
        std::cerr << "Buffer size should align with sizeof(uint64_t)"
                  << std::endl;
        return 1;
    }
    size_t buffer_length = buffer_size / sizeof(char);
    size_t alignment = std::max(sizeof(uint64_t), (size_t)512);
    char *write_buffer = (char *)aligned_alloc(alignment, buffer_size);
    char *write_buffer_next = (char *)aligned_alloc(alignment, buffer_size);
    char *read_buffer = (char *)aligned_alloc(alignment, buffer_size);

    // open the device
    int fd = open(device, O_LARGEFILE | O_RDWR | O_DIRECT);
    if (fd == -1) {
        perror("Could not open device");
        return 1;
    }

    // check its properties
    struct stat sb;
    if (fstat(fd, &sb)) {
        perror("Could not stat device");
        return 1;
    }
    if ((sb.st_mode & S_IFMT) != S_IFBLK) {
        std::cerr << "Device is not a block device" << std::endl;
        return 1;
    }

    // get device size and calculate block count
    uint64_t device_size = 0;
    if (ioctl(fd, BLKGETSIZE64, &device_size)) {
        perror("Could not get device size");
        return 1;
    }
    uint64_t blocks = device_size / block_size;

    // prepare the initial buffer
    fill(write_buffer_next, buffer_length, 0, f);
    strcpy(write_buffer_next, "START");

    // create the aio control block
    aiocb cb;
    memset(&cb, 0, sizeof(aiocb));
    cb.aio_fildes = fd;
    if (m == Mode::READ) {
        // write buffers are configured within the loop,
        // since we have two of them
        cb.aio_buf = read_buffer;
    }

    // main loop
    Progress indicator(blocks);
    indicator.start();
    for (uint64_t i = 0; i < blocks; i += blocks_at_once) {
        // swap buffers
        char *temp = write_buffer;
        write_buffer = write_buffer_next;
        write_buffer_next = temp;

        // prepare current data
        size_t current_buffer_size =
            block_size * std::min(blocks_at_once, (int)(blocks - i));
        if (i + blocks_at_once >= blocks)
            strcpy(write_buffer + current_buffer_size - 4, "STOP");

        // enqueue io
        cb.aio_offset = i * block_size;
        cb.aio_nbytes = current_buffer_size;
        if (m == Mode::READ) {
            if (aio_read(&cb)) {
                perror("aio_read submit");
                return 1;
            }
        } else if (m == Mode::WRITE) {
            cb.aio_buf = write_buffer;
            if (aio_write(&cb)) {
                perror("aio_write submit");
                return 1;
            }
        }

        // generate next data
        fill(write_buffer_next, buffer_size,
             cb.aio_offset + current_buffer_size, f);

        // process io
        while (aio_error(&cb) == EINPROGRESS) {
        }
        if ((errno = aio_error(&cb))) {
            perror("aio_read or aio_write");
            return -1;
        }
        if (m == Mode::READ) {
            if (memcmp(read_buffer, write_buffer, current_buffer_size)) {
                std::cerr << "Mismatch around block " << i << std::endl;
            }
        }

        indicator += blocks_at_once;
    }

    // clean-up
    free(read_buffer);
    free(write_buffer);
    free(write_buffer_next);
    close(fd);
    return 0;
}
