#include "lz4reader.h"
#include "zstdreader.h"
#include "xzreader.h"

#include "reader.h"

// LZ4 and zstd support contentSize.
#define CONTENTSIZE

#define LZ lz4
#include "op-lz-template.C"
#undef LZ

#define LZ zstd
#include "op-lz-template.C"
#undef LZ

// XZ does not support contentSize.
#undef CONTENTSIZE

#define LZ xz
#include "op-lz-template.C"
#undef LZ
