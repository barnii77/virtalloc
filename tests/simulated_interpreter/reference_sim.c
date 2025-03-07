#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>

#define NUM_REGISTERS (1 << 24)  // 16,777,216 possible registers

// Opcodes
#define OP_MALLOC  0x1
#define OP_REALLOC 0x2
#define OP_FREE    0x3

// The binary instruction is 8 bytes:
// 1 byte opcode, 3 bytes register (big endian), 2 bytes allocation size (big endian), and 2 bytes padding.
#pragma pack(push, 1)
typedef struct {
    uint8_t opcode;
    uint8_t reg[3];
    uint8_t size[2];
    uint8_t padding[2]; // not used
} instruction_t;
#pragma pack(pop)

// Helper: Convert 3 big-endian bytes to a 32-bit register ID (using only lower 24 bits)
uint32_t read_reg(const instruction_t *instr) {
    return ((uint32_t) instr->reg[0] << 16) |
           ((uint32_t) instr->reg[1] << 8) |
           ((uint32_t) instr->reg[2]);
}

// Helper: Convert 2 big-endian bytes to a 16-bit allocation size.
uint16_t read_size(const instruction_t *instr) {
    return ((uint16_t) instr->size[0] << 8) |
           ((uint16_t) instr->size[1]);
}

// Execute a single instruction using the registers array.
void run_instruction(const instruction_t *instr, void **registers) {
    uint32_t reg_id = read_reg(instr);
    uint16_t size = read_size(instr);

    switch (instr->opcode) {
        case OP_MALLOC:
            registers[reg_id] = malloc(size);
        // Optionally check for malloc failure here.
            break;
        case OP_REALLOC:
            if (size == 0) {
                // realloc with 0 is equivalent to free.
                free(registers[reg_id]);
                registers[reg_id] = NULL;
            } else {
                void *new_ptr = realloc(registers[reg_id], size);
                registers[reg_id] = new_ptr;
            }
            break;
        case OP_FREE:
            free(registers[reg_id]);
            registers[reg_id] = NULL;
            break;
        default:
            fprintf(stderr, "Unknown opcode: 0x%x\n", instr->opcode);
            break;
    }
}

// Run all instructions by iterating over the instruction array.
void run_instructions(const instruction_t *instructions, size_t count, void **registers) {
    for (size_t i = 0; i < count; i++) {
        run_instruction(&instructions[i], registers);
    }
}

int main(void) {
    // Allocate the registers array (zero-initialized).
    void **registers = calloc(NUM_REGISTERS, sizeof(void *));
    if (registers == NULL) {
        fprintf(stderr, "Failed to allocate registers array.\n");
        return 1;
    }

    // Open the binary file with instructions.
    const char *filename = "instructions.bin";
    FILE *fp = fopen(filename, "rb");
    if (fp == NULL) {
        fprintf(stderr, "Failed to open file: %s\n", filename);
        free(registers);
        return 1;
    }

    // Determine file size.
    fseek(fp, 0, SEEK_END);
    long file_size = ftell(fp);
    rewind(fp);

    // Verify that file size is a multiple of 8 (size of an instruction).
    if (file_size % sizeof(uint64_t) != 0) {
        fprintf(stderr, "Invalid file size: not a multiple of 8 bytes.\n");
        fclose(fp);
        free(registers);
        return 1;
    }
    size_t instruction_count = file_size / sizeof(uint64_t);

    // Load the entire file into memory.
    instruction_t *instructions = malloc(file_size);
    if (instructions == NULL) {
        fprintf(stderr, "Failed to allocate memory for instructions.\n");
        fclose(fp);
        free(registers);
        return 1;
    }

    if (fread(instructions, 1, file_size, fp) != file_size) {
        fprintf(stderr, "Failed to read the entire file.\n");
        fclose(fp);
        free(instructions);
        free(registers);
        return 1;
    }
    fclose(fp);

    // Process all instructions.
    run_instructions(instructions, instruction_count, registers);

    free(registers);
    free(instructions);
    return 0;
}
