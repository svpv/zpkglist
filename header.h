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
#include <sys/types.h>
#include <arpa/inet.h>

// These limits are forced by rpm 4.0 through 4.13.  When they are exceeded,
// rpm won't load the header.  The limits were relaxed in 4.14, the main reason
// being that "file signatures may require a lot of space in the header".
// Since headers in package lists are stripped, and typically do not include
// file signatures, and since we intend package lists to be usable with all
// versions of rpm, we stick to the older limits.
// Limits specified in rpm are actually off by one - e.g. the number of tags
// is exceeded if (ntags & 0xffff0000).  Here we use the more conventional
// meaning of "max" as "last valid", so the check should be (nags > Max).
#define headerMaxTags ((64<<10)-1)
#define headerMaxData ((16<<20)-1)

// rpm also specifies the "maximum no. of bytes permitted in a header",
// and sets the limit to 32M.  However, according to my reckoning,
// headerMaxTags and headerMaxData already cut the limit down to about 17M.
// Logic, as opposed to folly, never was a particular strength of rpm.
#define headerMaxSize (8 + 16 * headerMaxTags + headerMaxData)

// A header starts with magic (8 bytes) + (il,dl) sizes (8 bytes).
static const unsigned char headerMagic[8] = {
    0x8e, 0xad, 0xe8, 0x01, 0x00, 0x00, 0x00, 0x00
};

#define headerCheckMagic(lead) (memcmp(lead, headerMagic, 8) == 0)

// Returns the size of header's data after (il,dl).
static inline ssize_t headerDataSize(unsigned lead[4])
{
    unsigned il = ntohl(lead[2]);
    unsigned dl = ntohl(lead[3]);
    // Check the limits, further do not permit zero values.
    if (il - 1 > headerMaxTags - 1) return -1;
    if (dl - 1 > headerMaxData - 1) return -1;
    return 16 * il + dl;
}
