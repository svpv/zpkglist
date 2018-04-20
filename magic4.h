enum magic4 {
    MAGIC4_UNKNOWN = -1,
    MAGIC4_RPMHEADER,
    MAGIC4_ZPKGLIST,
    MAGIC4_ZSTD,
    MAGIC4_XZ,
};

// Unlike bswap, yields constant expr for a constant arg.
#define MAGIC4SWAP(x) \
    ((((x) & 0x000000ff) << 0x18) | \
     (((x) & 0xff000000) >> 0x18) | \
     (((x) & 0x0000ff00) << 0x08) | \
     (((x) & 0x00ff0000) >> 0x08))

#include <endian.h>
#if BYTE_ORDER && BYTE_ORDER == LITTLE_ENDIAN
#define MAGIC4LE(x) (x)
#define MAGIC4BE(x) MAGIC4SWAP(x)
#elif BYTE_ORDER && BYTE_ORDER == BIG_ENDIAN
#define MAGIC4LE(x) MAGIC4SWAP(x)
#define MAGIC4BE(x) (x)
#else
#error "unknown byte order"
#endif

#define MAGIC4_W_RPMHEADER      MAGIC4BE(0x8eade801)
#define MAGIC4_W_ZPKGLIST       MAGIC4LE(0x184d2a55)
#define MAGIC4_W_ZPKGLIST_DICT  MAGIC4LE(0x184d2a56)
#define MAGIC4_W_ZPKGLIST_DATA  MAGIC4LE(0x184d2a57)
#define MAGIC4_W_ZSTD           MAGIC4LE(0xfd2fb528)
#define MAGIC4_W_XZ             MAGIC4BE(0xfd377a58)

static inline
enum magic4 magic4(unsigned w)
{
    switch (w) {
    case MAGIC4_W_RPMHEADER: return MAGIC4_RPMHEADER;
    case MAGIC4_W_ZPKGLIST:  return MAGIC4_ZPKGLIST;
    case MAGIC4_W_ZSTD:      return MAGIC4_ZSTD;
    case MAGIC4_W_XZ:        return MAGIC4_XZ;
    }
    return MAGIC4_UNKNOWN;
}
