# DBF5 compression

## Codec boundary and ownership

`include/sdb/io/compression.hpp` is a standalone C++20 header. `Codec::None`
is always available; `Codec::Zstd` requires `SDB_WITH_ZSTD=1`, the real zstd SDK
headers and a real zstd library. A build without zstd rejects compressed blocks
and explicit zstd configuration, including input below the compression threshold.
There is no substitute codec. Legacy persistence and the versioned outer block
envelope remain the owning result writer's responsibility.

The codec API uses `std::span<const std::uint8_t>` and returns owned byte vectors:

```cpp
compression::Options options{compression::Codec::Zstd, 3, 256, true};
compression::Limits limits; // independent raw, stored and workspace caps
auto block = compression::encode(raw, options, limits);
auto decoded = compression::decode(block.codec, block.payload,
                                  block.raw_bytes, block.raw_crc, limits);
```

`Encoded` owns `codec`, `raw_bytes`, `raw_crc` and `payload`. The caller serializes
these into its versioned envelope and validates the declared stored byte count
against the span before invoking the decoder. `validate`, `available`,
`workspace_bound(options, raw_bytes)` and `decode_workspace_bound(codec)` allow
configuration validation and memory admission without allocation. Unknown codec
identifiers are rejected explicitly. Zstd levels 1 through 9 are supported.

Each raw block is limited to 4 MiB even if caller limits are larger. Default
stored capacity is 5 MiB and codec workspace has a hard 32 MiB cap. The encoder
charges `ZSTD_compressBound(raw.size())` against the stored budget before output
allocation; this is required even when the eventual compressed payload is small.
The caller separately charges raw storage, output capacity, envelope copies and
retained decoded data. Workspace estimates are conservative 32 MiB for each
eligible zstd block, zero for bypassed/None blocks. They cover zstd allocations,
not caller-owned vectors. Actual decoding can use a smaller caller workspace
budget when its context fits.

Zstd contexts use `ZSTD_createCCtx_advanced`/`ZSTD_createDCtx_advanced` with a
custom allocator. Every requested allocation includes an aligned size header in
the accounting and is checked before `malloc`; subtraction guards prevent size
overflow. Allocation failure throws, with context cleanup performed by RAII.
Codec work is synchronous with `ZSTD_c_nbWorkers=0`. The advanced allocator API
requires `ZSTD_STATIC_LINKING_ONLY` declarations, but the tested shared libraries
export the used functions; it does not require static linking. Consumers must
use a compatible SDK/library pair with these exports, rather than assuming every
zstd distribution exports the advanced API.

Zstd output always includes a known frame content size and the native zstd frame
checksum. A decoder rejects skippable frames, unknown/incorrect content size,
missing checksum, malformed/truncated frames, concatenated frames and trailing
bytes. Declared raw/stored limits, magic, checksum flag, frame span and content
size are checked before allocating decoded output. Decompression uses the
declared bounded destination, requires its exact decoded size, verifies the
zstd checksum and then checks the envelope's IEEE CRC32 of raw data. CRC is an
integrity check, not authentication. `None` requires identical raw/stored size
and verifies the same raw CRC.

## Real dependency verification (2026-10-29)

Only ignored local `build-deps/` files were used to supply development headers
and generated import libraries. No SDK header, runtime DLL, guessed declaration
or replacement implementation is vendored into the module. Official headers
preserve their upstream dual BSD/GPL notices. Provenance:

| Header | Official source | SHA256 |
| --- | --- | --- |
| zstd 1.5.7 `zstd.h` | [upstream v1.5.7](https://raw.githubusercontent.com/facebook/zstd/v1.5.7/lib/zstd.h) | `9B4BC8245565C98CCFC61C07749928B57E7C0F6FDDB0530C4F6AA1971893D88B` |
| zstd 1.5.7 `zstd_errors.h` | [upstream v1.5.7](https://raw.githubusercontent.com/facebook/zstd/v1.5.7/lib/zstd_errors.h) | `66A8C3F71D12EA6E797E4F622F31F3F8F81C41B36F48CAD4F5DE7D8BFB6AAC0A` |
| zstd 1.5.5 `zstd.h` | [upstream v1.5.5](https://raw.githubusercontent.com/facebook/zstd/v1.5.5/lib/zstd.h) | `41D0F43747D0DEE56F60BD10AED262F193D725B7E11EB9E94AA4AD80183C7DA8` |

Host-local development inputs, intentionally not a portable dependency contract:

- Windows runtime: `C:/Program Files/Git/mingw64/bin/libzstd.dll`, verified at
  runtime as 1.5.7. `objdump -p` verified every imported function exists. Matching
  headers live in `build-deps/zstd/include/`. GCC `dlltool` produced
  `build-deps/zstd/libzstd.dll.a`; MSVC `lib /def:... /machine:x64` produced
  `build-deps/zstd/zstd.lib` from a definition listing actual DLL exports.
- WSL Ubuntu 24.04: `/usr/lib/x86_64-linux-gnu/libzstd.so.1.5.5`, verified at
  runtime as 1.5.5. The development header and unversioned `.so` symlink were
  absent, so the official matching header was placed in ignored
  `build-deps/zstd-1.5.5/include/`, and the installed versioned `.so` was linked
  directly. No packages were installed.

For an ordinary SDK, provide its include directory and library to the optional
zstd CMake configuration. For a direct isolated check:

```sh
g++ -std=c++20 -O1 -Wall -Wextra -Wpedantic -Iinclude \
  tests/dbf5_codec_tests.cpp -o codec-none
g++ -std=c++20 -O1 -Wall -Wextra -Wpedantic -DSDB_WITH_ZSTD=1 \
  -Iinclude -I/path/to/zstd/include tests/dbf5_codec_tests.cpp \
  /path/to/real/libzstd.so -o codec-zstd
./codec-none
./codec-zstd
```

Both no-zstd and real-zstd codec tests passed with Windows GCC 15.2, Windows
MSVC 19.44 with AddressSanitizer, and WSL GCC 13.3. The WSL real-zstd test also
passed AddressSanitizer plus UndefinedBehaviorSanitizer. Dependency libraries
themselves were preinstalled release builds and were not rebuilt with sanitizers.
Coverage includes None, empty and incompressible input, threshold and
only-if-smaller bypass, all supported levels, maximum-size blocks, decoded-size
bombs, unknown/unavailable codecs, raw/stored/workspace limits, malformed,
truncated, concatenated and trailing frames, native checksum and raw CRC errors,
and decoding with less than the conservative workspace bound.

## Indexed result integration

CompressedIndexed4 wraps the existing typed logical blocks with a 24-byte
version/codec/raw-size/stored-size/raw-CRC envelope. Persisted default and per-field
policies are bound into file identity. Directories, commit footers, writer leases,
durability barriers and linked continuation reuse the existing infrastructure.
Normal reads and recovery cap stored block declarations before allocation;
recovery also rejects oversized truncated declarations before any tail repair.
Typed rows are split adaptively to respect the 4 MiB raw codec input limit.
This is a codec-block bound, not a claim that every model/frame allocation is
4 MiB: frame encoding/validation has its existing admission charge. Async admission
adds conservative codec/output scratch before taking frame ownership.

Integration tests cover seven lanes, signed zero/missingness, mixed raw/zstd
policy, selective reads, append, corruption, oversize recovery, low budgets and
wide-row splitting. CLI and separate-process crash/independent wire-oracle tests
are detailed in DBF5_VERIFICATION. The formal byte grammar is RESULT_FORMAT_SPEC.

Production integration, CUDA, physical power-loss survival and untested zstd
distributions remain outside this evidence. Migration must preserve the bounded
single-frame/raw-CRC contract and make the actual zstd runtime discoverable.
