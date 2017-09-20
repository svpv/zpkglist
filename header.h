// Copyright (c) 2017 Alexey Tourbin
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.

// RPM header utilities.

#include <stdbool.h>
#include <string.h>
#include <arpa/inet.h>

// Maximum bytes in a header permitted by rpm, without magic.
#define headerMaxSize (32 << 20)

// A header starts with magic (8 bytes) + (il,dl) sizes (8 bytes).
static const unsigned char headerMagic[8] = {
    0x8e, 0xad, 0xe8, 0x01, 0x00, 0x00, 0x00, 0x00
};

#define headerCheckMagic(lead) (memcmp(lead, headerMagic, 8) == 0)

// Returns the size of header's data after (il,dl).
static inline int headerDataSize(char lead[16])
{
    unsigned *ei = (unsigned *) (lead + 8);
    unsigned il = ntohl(ei[0]);
    unsigned dl = ntohl(ei[1]);
    // Check for overflows.
    if (il > headerMaxSize / 16 || dl > headerMaxSize)
	return -1;
    size_t dataSize = 16 * il + dl;
    if (dataSize == 0 || 8 + dataSize > headerMaxSize)
	return -1;
    return dataSize;
}
