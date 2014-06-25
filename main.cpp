
/*
burndisk
--------

-r: read
-w: write
-x: combined

-z: zeros
-h: hash, based on byte offset
-i: iterate, write byte offset

-s: sequential
-r: random

-b: block size
-c: blocks-at-once

device

don't truncate
multiple next buffers, in multithreaded fashion?
 */

#define _LARGEFILE64_SOURCE

// Linux API's
#include <sys/ioctl.h>
#include <sys/types.h>
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
#include "xxhash.h"

const char *device = "/dev/sda";
const int block_size = 4096;
const int blocks_at_once = 8192;

static uint64_t r = 1442695040888963407UL;

static inline uint64_t xorshift() {
  r ^= (r >> 17);
  r ^= (r << 31);
  r ^= (r >> 8);
  return r;
}

void fillrandom(char *buffer, size_t bufferSize) {
  for (size_t index = 0; index < bufferSize; index++)
    *(buffer + index) = (char)xorshift();
}

int main(int argc, char **argv) {
  // parse options
  bool do_read = true;
  int c;
  while ((c = getopt(argc, argv, "rw")) != -1) {
    switch (c) {
    case 'r':
      do_read = true;
      break;
    case 'w':
      do_read = false;
      break;
    default:
      return -1;
      break;
    }
  }
  std::cout << "Program mode: " << (do_read ? "read" : "write") << std::endl;

  // calculate buffer sizes
  size_t buffer_size = block_size * blocks_at_once;
  size_t buffer_length = buffer_size / sizeof(char);
  char *write_buffer = (char *)valloc(buffer_size);
  char *write_buffer_next = (char *)valloc(buffer_size);
  char *read_buffer = (char *)valloc(buffer_size);

  // open the device
  int fd = open(device, O_LARGEFILE | O_RDWR | O_DIRECT);
  if (fd == -1) {
    perror("open");
    return 1;
  }

  // get device size and calculate block count
  uint64_t device_size = 0;
  if (ioctl(fd, BLKGETSIZE64, &device_size)) {
    perror("BLKGETSIZE");
    return 1;
  }
  if (device_size % buffer_size != 0) {
    uint64_t compatible_device_size = (device_size / buffer_size) * buffer_size;
    std::cerr << "WARNING: incompatible device size, truncating "
              << (device_size - compatible_device_size) << " bytes"
              << std::endl;
  }
  uint64_t blocks = device_size / block_size;

  // prepare the initial buffer
  fillrandom(write_buffer_next, buffer_length);
  strcpy(write_buffer_next, "START");

  // create the aio control block
  aiocb cb;
  memset(&cb, 0, sizeof(aiocb));
  cb.aio_nbytes = buffer_size;
  cb.aio_fildes = fd;
  cb.aio_buf = read_buffer;

  // main loop
  Progress indicator(blocks);
  indicator.start();
  for (uint64_t i = 0; i < blocks; i += blocks_at_once) {
    // swap buffers
    char *temp = write_buffer;
    write_buffer = write_buffer_next;
    write_buffer_next = temp;

    // enqueue io
    cb.aio_offset = i * block_size;
    if (do_read) {
      if (aio_read(&cb)) {
        perror("aio_read submit");
        return 1;
      }
    } else {
      cb.aio_buf = write_buffer;
      if (aio_write(&cb)) {
        perror("aio_write submit");
        return 1;
      }
    }

    // generate next data
    fillrandom(write_buffer_next, buffer_size);
    if (i + blocks_at_once >= blocks)
      strcpy(write_buffer_next + buffer_size - 4, "STOP");

    // process io
    while (aio_error(&cb) == EINPROGRESS) {
    }
    if (aio_error(&cb)) {
      perror("aio_read or aio_write");
      return -1;
    }
    if (do_read) {
      if (memcmp(read_buffer, write_buffer, buffer_size)) {
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
