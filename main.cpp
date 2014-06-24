#define _LARGEFILE64_SOURCE

#include <iostream>
#include <fcntl.h>
#include <linux/fs.h>
#include <sys/ioctl.h>
#include <iomanip>
#include <vector>
#include <ctime>
#include <sstream>
#include <sys/types.h>
#include <unistd.h>
#include <cassert>
#include <cstring>

#include "progress.hpp"

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
  char *write_buffer = (char*) valloc(buffer_size);
  char *read_buffer = (char*) valloc(buffer_size);

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
  uint64_t blocks = device_size / block_size;

  // main loop
  Progress indicator(blocks);
  indicator.start();
  for (uint64_t i = 0; i < blocks; i += blocks_at_once) {
    // check current offset
    off_t offset = lseek(fd, 0, SEEK_CUR);
    assert(offset == (int64_t)(i * block_size));

    // generate data
    uint64_t current_blocks = std::min((uint64_t)blocks_at_once, blocks - i);
    uint64_t current_buffer_length = current_blocks * block_size;
    fillrandom(write_buffer, current_buffer_length);
    if (i == 0)
      strcpy(write_buffer, "START");
    else if (i + blocks_at_once >= blocks)
      strcpy(write_buffer + current_buffer_length - 4, "STOP");

    // process data
    if (do_read) {
      read(fd, read_buffer, current_buffer_length);
      if (errno) {
        perror("read");
        return 1;
      }
      if (memcmp(read_buffer, write_buffer, current_buffer_length)) {
        std::cerr << "Mismatch around block " << i << std::endl;
      }
    } else {
      write(fd, write_buffer, buffer_size);
      if (errno) {
        perror("write");
        return 1;
      }
    }

    indicator += blocks_at_once;
  }

  // clean-up
  free(read_buffer);
  free(write_buffer);
  close(fd);
  return 0;
}
