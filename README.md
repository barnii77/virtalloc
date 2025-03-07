# About
This project is a tiny heap allocator library for learning purposes. I primarily wanted to try coming up with a decently efficient allocation algorithm.

I ended up with an allocator that reaches similar speeds as glibc (generally slightly slower, but realloc appears to be faster than glibc, I suspect because glibc makes alignment guarantees that I don't make?). My allocator likely has more metadata overhead though.

# Building
See [BUILDING.md](docs/BUILDING.md)

# Running tests
See [TESTING.md](docs/TESTING.md)

# License
MIT License