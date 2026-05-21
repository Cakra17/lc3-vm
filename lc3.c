#include <signal.h>
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
#define MEMORY_MAX (1 << 16) // Maximum size is 2**16 or 65536
uint16_t memory[MEMORY_MAX];

// Register
#define REGISTER_NUM 10
enum
{
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
enum
{
  OP_BR = 0, // branch
  OP_ADD,    // add
  OP_LD,     // load
  OP_ST,     // store
  OP_JSR,    // jump register
  OP_AND,    // bitwise and
  OP_LDR,    // load register
  OP_STR,    // store register
  OP_RTI,    // unused
  OP_NOT,    // bitwise not
  OP_LDI,    // load indirect
  OP_STI,    // store indirect
  OP_JMP,    // jump
  OP_RES,    // reserved (unused)
  OP_LEA,    // load executive address
  OP_TRAP    // executive trap
};

// Conditional Flags
enum
{
  FL_POS = 1 << 0, // Positive
  FL_ZR = 1 << 1,  // Zero
  FL_NEG = 1 << 2, // Negative
};

// Memory Register
enum
{
  MR_KBSR = 0xFE00, // keyboard status register
  MR_KBDR = 0xFE02, // keyboard data register
};

enum
{
  TRAP_GETC = 0x20,  // get character from keyboard, not echoed onto the terminal
  TRAP_OUT = 0x21,   // output a character
  TRAP_PUTS = 0x22,  // output a word string
  TRAP_IN = 0x23,    // get character from keyboard, echoed onto the terminal
  TRAP_PUTSP = 0x24, // output a byte string
  TRAP_HALT = 0x25   // halt the program
};

// Non blocking input
struct termios original_tio,
    new_tio;

void disable_input_buffering()
{
  tcgetattr(STDIN_FILENO, &original_tio);
  new_tio = original_tio;
  new_tio.c_cflag &= ~ICANON & ~ECHO;
  tcsetattr(STDIN_FILENO, TCSANOW, &new_tio);
}

void restore_input_buffering()
{
  tcsetattr(STDIN_FILENO, TCSANOW, &original_tio);
}

uint16_t check_key()
{
  fd_set readfds;
  FD_ZERO(&readfds);
  FD_SET(STDIN_FILENO, &readfds);

  struct timeval timeout;
  timeout.tv_sec = 0;
  timeout.tv_usec = 0;
  return select(1, &readfds, NULL, NULL, &timeout) != 0;
}

void handle_interupt()
{
  restore_input_buffering();
  printf("\n");
  exit(-2);
}

// lc3 using big endian, but most of the modern computer is using little endian
// so we have to swap the bit position to convert big endian to little endian
// resource: https://en.wikipedia.org/wiki/Endianness
uint16_t swap16(uint16_t x)
{
  return (x << 8) | (x >> 8);
}

void adjust_layout(FILE *file)
{
  uint16_t buffer;
  fread(&buffer, sizeof(uint16_t), 1, file); // get the first 16 bit
  buffer = swap16(buffer);                   // swap big endian to small edian

  uint16_t max_read = MEMORY_MAX - buffer;                  // calculate how many words to read
  uint16_t *p = memory + buffer;                            // get the starting position using pointer aritmatic, memory + buffer is equal to memory[buffer]
  size_t read = fread(p, sizeof(uint16_t), max_read, file); // bulk read all the remaining 16-bit words

  // swap endianness from big endian to small endian
  // for every 16-bit words
  while (read-- > 0)
  {
    *p = swap16(*p);
    ++p;
  }
}

void mem_write(uint16_t address, uint16_t value)
{
  memory[address] = value;
}

uint16_t mem_read(uint16_t address)
{
  if (address == MR_KBSR)
  {
    if (check_key())
    {
      memory[MR_KBSR] = (1 << 15);
      memory[MR_KBDR] = getchar();
    }
    else
    {
      memory[MR_KBSR] = 0;
    }
  }
  return memory[address];
}

int read_file(const char *path)
{
  FILE *file = fopen(path, "rb");
  if (!file)
    return 0;
  adjust_layout(file);
  fclose(file);
  return 1;
}

uint16_t sign_extend(uint16_t x, int bit_count)
{
  if ((x >> (bit_count - 1)) & 1)
  {
    x |= (0xFFFF << bit_count);
  }
  return x;
}

void update_flags(uint16_t x)
{
  if (reg[x] == 0)
  {
    reg[R_COND] = FL_ZR;
  }
  else if (reg[x] >> 15)
  {
    reg[R_COND] = FL_NEG;
  }
  else
  {
    reg[R_COND] = FL_POS;
  }
}

int main(int argc, char *argv[])
{
  // load arguments
  if (argc < 2)
  {
    printf("lc3 [file]\n");
    exit(1);
  }

  for (int i = 0; i < argc; i++)
  {
    if (!read_file(argv[i]))
    {
      printf("Failed to load file %s!!\n", argv[i]);
    }
  }

  // activate non-blocking input
  signal(SIGINT, handle_interupt);
  disable_input_buffering();

  // main logic
  // since exactly one condition flag should be set at any given time, set the Z flag
  reg[R_COND] = FL_ZR;

  // set the PC to starting position
  // 0x3000 is the default
  enum
  {
    PC_START = 0x3000
  };
  reg[R_PC] = PC_START;

  int running = 1;
  while (running)
  {
    uint16_t instr = mem_read(reg[R_PC]++);
    uint16_t opcode = instr >> 12;

    switch (opcode)
    {
    case OP_ADD:
      // specification: https://www.jmeiners.com/lc3-vm/supplies/lc3-isa.pdf, page 526
      // Destination register (DR)
      uint16_t dr = (instr >> 9) & 0x7;
      // First Operand (SR1)
      uint16_t first = (instr >> 6) & 0x7;
      // immediate flag
      uint16_t flag = (instr >> 5) & 0x1;

      if (flag)
      {
        uint16_t imm5 = sign_extend(instr & 0x1F, 5);
        reg[dr] = reg[first] + imm5;
      }
      else
      {
        uint16_t second = instr & 0x7;
        reg[dr] = reg[first] + reg[second];
      }

      update_flags(dr);
      break;
    case OP_AND:
      // specification: https://www.jmeiners.com/lc3-vm/supplies/lc3-isa.pdf, page 527
      // Destination register (DR)
      dr = (instr >> 9) & 0x7;
      // First Operand (SR1)
      first = (instr >> 6) & 0x7;
      // immediate flag
      flag = (instr >> 5) & 0x1;

      if (flag)
      {
        uint16_t imm5 = sign_extend(instr & 0x1F, 5);
        reg[dr] = reg[first] & imm5;
      }
      else
      {
        uint16_t second = instr & 0x7;
        reg[dr] = reg[first] & reg[second];
      }

      update_flags(dr);
      break;
    case OP_NOT:
      // spesification: https://www.jmeiners.com/lc3-vm/supplies/lc3-isa.pdf, page 535
      // Destination register (DR)
      dr = (instr >> 9) & 0x7;
      uint16_t sr = (instr >> 6) & 0x7;

      reg[dr] = ~reg[sr];
      update_flags(dr);
      break;
    case OP_BR:
      // spesification: https://www.jmeiners.com/lc3-vm/supplies/lc3-isa.pdf, page 528
      uint16_t pc_offset = sign_extend(instr & 0x1FF, 9);
      uint16_t condFlag = (instr >> 9) & 0x7;
      if (condFlag & reg[R_COND])
      {
        reg[R_PC] += pc_offset;
      }
      break;
    case OP_JMP:
      // spesification: https://www.jmeiners.com/lc3-vm/supplies/lc3-isa.pdf, page 529
      uint16_t base_r = (instr >> 6) & 0x7;
      reg[R_PC] = reg[base_r];
      break;
    case OP_JSR:
      // spesification: https://www.jmeiners.com/lc3-vm/supplies/lc3-isa.pdf, page 530
      reg[R_7] = reg[R_PC];
      uint16_t mode = (instr >> 11) & 0x1;
      if (mode)
      {
        // JSR
        uint16_t pc_offset = sign_extend(instr & 0x7FF, 11);
        reg[R_PC] += pc_offset;
      }
      else
      {
        // JSRR
        uint16_t base_r = (instr >> 6) & 0x7;
        reg[R_PC] = reg[base_r];
      }
      break;
    case OP_LD:
      // spesification: https://www.jmeiners.com/lc3-vm/supplies/lc3-isa.pdf, page 531
      dr = (instr >> 9) & 0x7;
      pc_offset = sign_extend(instr & 0x1FF, 9);
      reg[dr] = mem_read(reg[R_PC] + pc_offset);
      update_flags(dr);
      break;
    case OP_LDI:
      // specification: https://www.jmeiners.com/lc3-vm/supplies/lc3-isa.pdf, page 532
      // Destination register (DR)
      dr = (instr >> 9) & 0x7;
      // offset
      pc_offset = sign_extend(instr & 0x1FF, 9);
      reg[dr] = mem_read(mem_read(reg[R_PC] + pc_offset));
      update_flags(dr);
      break;
    case OP_LDR:
      // spesification: https://www.jmeiners.com/lc3-vm/supplies/lc3-isa.pdf, page 533
      dr = (instr >> 9) & 0x7;
      base_r = (instr >> 6) & 0x7;
      pc_offset = sign_extend(instr & 0x3F, 6);
      reg[dr] = mem_read(reg[base_r] + pc_offset);
      update_flags(dr);
      break;
    case OP_LEA:
      // spesification: https://www.jmeiners.com/lc3-vm/supplies/lc3-isa.pdf, page 534
      // Destination register (DR)
      dr = (instr >> 9) & 0x7;
      // offset
      pc_offset = sign_extend(instr & 0x1FF, 9);
      reg[dr] = reg[R_PC] + pc_offset;
      update_flags(dr);
      break;
    case OP_ST:
      // spesification: https://www.jmeiners.com/lc3-vm/supplies/lc3-isa.pdf, page 538
      sr = (instr >> 9) & 0x7;
      pc_offset = sign_extend(instr & 0x1FF, 9);
      mem_write(reg[R_PC] + pc_offset, reg[sr]);
      break;
    case OP_STI:
      // spesification: https://www.jmeiners.com/lc3-vm/supplies/lc3-isa.pdf, page 539
      sr = (instr >> 9) & 0x7;
      pc_offset = sign_extend(instr & 0x1FF, 9);
      mem_write(mem_read(reg[R_PC] + pc_offset), reg[sr]);
      break;
    case OP_STR:
      // spesification: https://www.jmeiners.com/lc3-vm/supplies/lc3-isa.pdf, page 540
      sr = (instr >> 9) & 0x7;
      base_r = (instr >> 6) & 0x7;
      pc_offset = sign_extend(instr & 0x3F, 6);
      mem_write(reg[base_r] + pc_offset, reg[sr]);
      break;
    case OP_TRAP:
      reg[R_7] = reg[R_PC];
      switch (instr & 0xFF)
      {
      case TRAP_GETC:
        reg[R_0] = (uint16_t)getchar();
        update_flags(R_0);
        break;
      case TRAP_OUT:
        uint16_t ch = reg[R_0];
        putc((char)ch, stdout);
        break;
      case TRAP_PUTS:
        uint16_t *c = memory + reg[R_0];
        while (*c)
        {
          putc((char)*c, stdout);
          ++c;
        }
        fflush(stdout);
        break;
      case TRAP_IN:
        printf("Enter a character: ");
        char chr = getchar();
        putc(chr, stdout);
        fflush(stdout);
        reg[R_0] = (uint16_t)chr;
        update_flags(R_0);
        break;
      case TRAP_PUTSP:
        c = memory + reg[R_0];
        while (*c)
        {
          char ch1 = (*c) >> 0x7;
          putc(ch1, stdout);
          char ch2 = (*c) >> 8;
          if (ch2)
            putc(ch2, stdout);
          ++c;
        }
        fflush(stdout);
        break;
      case TRAP_HALT:
        puts("HALT");
        fflush(stdout);
        running = 0;
        break;
      }
      break;
    case OP_RES:
    case OP_RTI:
    default:
      abort();
      break;
    }
  }

  // disable non-blocking input
  restore_input_buffering();
  return 0;
}
