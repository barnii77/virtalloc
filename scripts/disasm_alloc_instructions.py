OP_MALLOC  = 0x1
OP_REALLOC = 0x2
OP_FREE    = 0x3
ENDIAN = "big"


def fmt_instr(instr: str) -> str:
    out = ""

    is_free = False
    # emit op
    op_byte = instr[0]
    if op_byte == OP_MALLOC:
        out += "malloc"
    elif op_byte == OP_FREE:
        out += "free"
        is_free = True
    elif op_byte == OP_REALLOC:
        out += "realloc"
    else:
        raise RuntimeError("unexpected opcode")

    # emit register
    out += " r"
    out += str(int.from_bytes(instr[1:4], ENDIAN))

    # emit size
    if not is_free:
        out += " size "
        out += str(int.from_bytes(instr[4:6], ENDIAN))

    return out



def main():
    output_file = "../tests/simulated_interpreter/instructions.bin"
    with open(output_file, "rb") as f:
        content = f.read()
        for i in range(int(len(content) / 8)):
            instr = content[i * 8 : (i + 1) * 8]
            print(fmt_instr(instr))


if __name__ == "__main__":
    main()
