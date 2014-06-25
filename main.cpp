
/*
TODO

-x: combined

-t tune

multiple next buffers, in multithreaded fashion?
 */

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

// Local
#include "progress.hpp"

enum class Task {
    write,
    verify
};

enum class Mode {
    sequential,
    random
};

enum class Fill {
    zero,
    index,
    hash
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

static void fill(char *buffer, size_t bufferSize, uint64_t offset, Fill f) {
    unsigned int chars_per_uint64 = sizeof(uint64_t) / sizeof(char);
    switch (f) {
    case Fill::zero:
        for (size_t index = 0; index < bufferSize; index += chars_per_uint64)
            *(uint64_t *)(buffer + index) = 0;
        break;
    case Fill::index:
        for (size_t index = 0; index < bufferSize; index += chars_per_uint64)
            *(uint64_t *)(buffer + index) = offset + index;
        break;
    case Fill::hash:
        for (size_t index = 0; index < bufferSize; index += chars_per_uint64)
            *(uint64_t *)(buffer + index) = hash64shift(offset + index);
        break;
    }
}

int main(int argc, char **argv) {
    // parse options
    Task t = Task::verify;
    Mode m = Mode::sequential;
    Fill f = Fill::zero;
    int block_size = 4096;
    int blocks_at_once = 8192;
    int c;
    optarg = NULL;
    while ((c = getopt(argc, argv, "wvsrzihb:c:")) != -1) {
        switch (c) {
        // burn-in tasks
        case 'w':
            t = Task::write;
            break;
        case 'v':
            t = Task::verify;
            break;
        // scanning modes
        case 's':
            m = Mode::sequential;
            break;
        case 'r':
            m = Mode::random;
            std::cerr << "Random mode not implemented yet" << std::endl;
            return 1;
            break;
        // fill modes
        case 'z':
            f = Fill::zero;
            break;
        case 'i':
            f = Fill::index;
            break;
        case 'h':
            f = Fill::hash;
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
    std::cout << "Burn-in task: " << (t == Task::write ? "write" : "verify")
              << std::endl;

    // calculate buffer sizes
    size_t buffer_size = block_size * blocks_at_once;
    if (buffer_size % sizeof(uint64_t) != 0) {
        std::cerr << "Buffer size should align with sizeof(uint64_t)"
                  << std::endl;
        return 1;
    }
    // TODO: BLKSSZGET or pagesize?
    size_t buffer_length = buffer_size / sizeof(char);
    size_t alignment = std::max(sizeof(uint64_t), (size_t)getpagesize());
    char *write_buffer, *write_buffer_next, *read_buffer;
    posix_memalign((void **)&write_buffer, alignment, buffer_size);
    posix_memalign((void **)&write_buffer_next, alignment, buffer_size);
    if (t == Task::verify)
        posix_memalign((void **)&read_buffer, alignment, buffer_size);

    // open the device
    int fd = open(device, O_RDWR | O_DIRECT);
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
    strncpy(write_buffer_next, "START", 5);

    // main loop
    Progress indicator(blocks);
    indicator.start();
    for (uint64_t i = 0; i < blocks; i += blocks_at_once) {
        // check current offset
        off_t offset = lseek(fd, 0, SEEK_CUR);
        assert(offset == (int64_t)(i * block_size));

        // swap buffers
        char *temp = write_buffer;
        write_buffer = write_buffer_next;
        write_buffer_next = temp;
        size_t current_buffer_size =
            block_size * std::min(blocks_at_once, (int)(blocks - i));

        bool error = false;
#pragma omp parallel sections
        {
#pragma omp section
            {
                // prepare current data
                if (i + blocks_at_once >= blocks)
                    strncpy(write_buffer + current_buffer_size - 4, "STOP", 4);

                // perform io
                if (t == Task::verify) {
                    read(fd, read_buffer, current_buffer_size);
                    if (errno) {
                        perror("Could not read data");
                        error = true;
                    }
                } else if (t == Task::write) {
                    write(fd, write_buffer, current_buffer_size);
                    if (errno) {
                        perror("Could not write data");
                        error = true;
                    }
                }
            }
#pragma omp section
            {
                // generate next data
                fill(write_buffer_next, buffer_size,
                     offset + current_buffer_size, f);
            }
        }
        if (error)
            return 1;

        // process io
        if (t == Task::verify) {
            if (memcmp(read_buffer, write_buffer, current_buffer_size)) {
                std::cerr << "Mismatch around block " << i << std::endl;
            }
        }

        indicator += blocks_at_once;
    }

    // clean-up
    if (t == Task::verify)
        free(read_buffer);
    free(write_buffer);
    free(write_buffer_next);
    close(fd);
    return 0;
}
