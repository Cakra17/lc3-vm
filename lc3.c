#include <stddef.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>

#include <stdlib.h>
#include <termios.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/termios.h>
#include <sys/mman.h>

// Memory
#define MEMORY_MAX 1 << 16// Maximum size is 2**16 or 65536
uint16_t memory[MEMORY_MAX];

// Register
#define REGISTER_NUM 10
enum {
  // General Purpose Register
  R_0 = 0,
  R_1,
  R_2,
  R_3,
  R_4,
  R_5,
  R_6,
  R_7,

  // Conditional Register
  R_PC,   // Program Counter
  R_COND, // Conditional Flag
};
uint16_t reg[REGISTER_NUM];

// instruction set (OPCODES)
enum {
  OP_BR = 0,  // branch
  OP_ADD,     // add 
  OP_LD,      // load
  OP_ST,      // store
  OP_JSR,     // jump register
  OP_AND,     // bitwise and
  OP_LDR,     // load register
  OP_STR,     // store register
  OP_RTI,     // unused
  OP_NOT,     // bitwise not
  OP_LDI,     // load indirect 
  OP_STI,     // store indirect
  OP_JMP,     // jump
  OP_RES,     // reserved (unused)
  OP_LEA,     // load executive address
  OP_TRAP     // executive trap
};

// Conditional Flags
enum {
  FL_POS  = 1 << 0, // Positive
  FL_ZR   = 1 << 1, // Zero
  FL_NEG  = 1 << 2, // Negative
};

// Memory Register
enum {
  MR_KBSR = 0xFE00, // keyboard status register
  MR_KBDR = 0xFE02, // keyboard data register
};

// input buffering
struct termios original_tio;

void disable_input_buffering() {
  tcgetattr(STDIN_FILENO, &original_tio);
  struct termios new_tio = original_tio;
  new_tio.c_cflag &= ~ICANON & ~ECHO;
  tcsetattr(STDIN_FILENO, TCSANOW, &new_tio);
}

void restore_input_buffering() {
  tcsetattr(STDIN_FILENO, TCSANOW, &original_tio);
}

uint16_t check_key() {
  fd_set readfds;
  FD_ZERO(&readfds);
  FD_SET(STDIN_FILENO, &readfds);

  struct timeval timeout;
  timeout.tv_sec = 0;
  timeout.tv_usec = 0;
  return select(1, &readfds, NULL, NULL, &timeout) != 0;
}

// lc3 using big endian, but most of the modern computer is using little endian
// so we have to swap the bit position to convert little endian to big endian
// resource: https://en.wikipedia.org/wiki/Endianness
uint16_t swap16(uint16_t x) {
  return (x << 8) || (x >> 8);
}

void adjust_layout(FILE *file) {
  uint16_t buffer;
  fread(&buffer, sizeof(uint16_t), 1, file);
  buffer = swap16(buffer);

  uint16_t max_read = (MEMORY_MAX) - buffer;
  uint16_t *p = memory + buffer;
  size_t read = fread(p, sizeof(uint16_t), max_read, file);

  while (read-- > 0) {
    *p = swap16(*p);
    ++p;
  }
}

void mem_write(uint16_t address, uint16_t value) {
  memory[address] = value;
}

uint16_t mem_read(uint16_t address) { 
  if (address == MR_KBSR) {
    if (check_key()) {
      memory[MR_KBSR] = (1 << 15);
      memory[MR_KBDR] = getchar();
    } else {
      memory[MR_KBSR] = 0;
    }
  }
  return memory[address];
}


int read_file(const char *path) {
  FILE* file = fopen(path, "rb");
  if (!file) return 0;
  adjust_layout(file);
  fclose(file);
  return 1;
}

int main(int argc, char* argv[]) {
  // load arguments
  if (argc < 2) {
    printf("lc3 [file]\n");
    exit(1);
  }

  for (int i = 0; i < argc; i++) {
    if (!read_file(argv[i])) {
      printf("Failed to load file %s!!\n", argv[i]);
    } 
  } 

  // setup
  return 0;
}
