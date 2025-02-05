#ifndef SMALL_RR_MEMORY_SLOT_META_H
#define SMALL_RR_MEMORY_SLOT_META_H

typedef struct SmallRRMemorySlotMeta {
    unsigned char is_free: 1;
    unsigned char meta_type: 7;  // 3 for this struct type
} SmallRRMemorySlotMeta;

/// used for deallocation
typedef struct SmallRRStartOfMemoryChunkMeta {
    /// this is actually a void* stored as raw bytes for alignment reasons
    char memory_chunk_ptr_raw_bytes[sizeof(void *)];
    unsigned char must_release_chunk_on_destroy: 1;
    char __padding[63 - sizeof(void *) - sizeof(unsigned char)];  // pad this to 63 bytes
} SmallRRStartOfMemoryChunkMeta;

typedef struct SmallRRNextSlotLink {
    unsigned char __bit_padding: 1;
    unsigned char meta_type: 7;  // 4 for this struct type
} SmallRRNextSlotLinkMeta;

#endif
