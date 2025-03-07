#!/usr/bin/env python3
import random
import sys

# Define opcodes.
OP_MALLOC  = 0x1
OP_REALLOC = 0x2
OP_FREE    = 0x3

N_REGS_IN_USE = 4096  # tested with [32, 4096]

def pack_instruction(opcode, reg, size):
    """
    Pack an instruction into 8 bytes:
      1 byte: opcode
      3 bytes: register (allocation ID) in big-endian order
      2 bytes: allocation size (big-endian)
      2 bytes: padding (zeros)
    """
    opcode_byte = opcode.to_bytes(1, byteorder='big')
    reg_bytes = reg.to_bytes(3, byteorder='big')
    size_bytes = size.to_bytes(2, byteorder='big')
    padding = b'\x00\x00'
    return opcode_byte + reg_bytes + size_bytes + padding

def main():
    # Total number of instructions to generate.
    n_instructions = int(sys.argv[1]) if len(sys.argv) > 1 else 100_000

    # Track allocation state for each register.
    # If allocated[reg] is True, the register is allocated; if not present or False, it is free.
    allocated = {}

    output_file = "../tests/simulated_interpreter/instructions.bin"
    with open(output_file, "wb") as f:
        for _ in range(n_instructions):
            # Generate a random register (3 bytes but not all 16M registers are used)
            reg = random.randrange(0, N_REGS_IN_USE)
            # Generate a random 2-byte size (0 <= size < 2^16)
            size = random.getrandbits(16)

            # If the register is free, we must use malloc.
            # If it is allocated, choose randomly between realloc and free.
            if allocated.get(reg, False):
                # Randomly choose: 0 => free, 1 => realloc.
                opcode = OP_FREE if random.getrandbits(1) == 0 else OP_REALLOC
                # NOTE: interestingly, if I disable realloc, glibc outperforms my allocator. If realloc is enabled,
                # I typically outperform glibc.
                # opcode = OP_FREE  # disable realloc for testing
            else:
                opcode = OP_MALLOC

            # Update allocation state:
            # - For malloc: the register becomes allocated.
            # - For free: the register becomes free.
            # - For realloc: if size is 0, it is a free operation, otherwise it remains allocated.
            allocated[reg] = opcode in (OP_MALLOC, OP_REALLOC) and size != 0

            # Pack the instruction and write it.
            f.write(pack_instruction(opcode, reg, size))

    print(f"Generated {n_instructions} instructions and written to {output_file}")

if __name__ == "__main__":
    main()
