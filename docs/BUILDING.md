# Build script options

- `VIRTALLOC_SSE4`
- `VIRTALLOC_LOGGING`
- `VIRTALLOC_EXTERNAL_ASSERTS_ONLY`
- `VIRTALLOC_NDEBUG`

# Debug build
`cmake -B build && make -C build`

or explicit:

`cmake -CMAKE_BUILD_TYPE=Debug -B build && make -C build`

# Release build
`cmake -CMAKE_BUILD_TYPE=Release -B build && make -C build`

# Build with SSE 4 accelerated CRC32 (*highly recommended*)
Should be turned on unless targeting extremely cheap/old CPUs, e.g. 30-year-old computers or microcontrollers.

`cmake -DVIRTALLOC_SSE4=ON -B build && make -C build` (for debug build)

or for release build:

`cmake -DVIRTALLOC_SSE4=ON -CMAKE_BUILD_TYPE=Release -B build && make -C build` (for release build)

# Build with verbose logging
`cmake -DVIRTALLOC_LOGGING=ON -B build && make -C build`

# Build with internal asserts (catches library bugs)
`cmake -DVIRTALLOC_EXTERNAL_ASSERTS_ONLY=OFF -B build && make -C build`

# Build with NDEBUG
`cmake -DVIRTALLOC_NDEBUG=ON -B build && make -C build`