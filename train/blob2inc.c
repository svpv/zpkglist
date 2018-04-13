// Copyright (c) 2017, 2018 Alexey Tourbin
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

// This program converts a blob into a C header.
// Usage: blob2inc [-z] [-Z] arrayName <blob >hdr.h
//	  -z  compress with LZ4
//	  -Z  make zpkglist dictionary frame

#include <stdio.h>
#include <stdbool.h>
#include <string.h>
#include <unistd.h>
#include <assert.h>
#include <sys/stat.h>
#include <endian.h>
#include <lz4hc.h>

// ASCII range from ' ' to '~'.
#define xisprint(c) ((unsigned)(c)-' ' <= '~'-' ')

int main(int argc, char **argv)
{
    bool compress = false;
    int frameSize = 0;
    int opt;
    while ((opt = getopt(argc, argv, "zZ")) != -1) {
	switch (opt) {
	case 'Z': frameSize = 8; // fall through
	case 'z': compress = 1; break;
	default: return 1;
	}
    }
    argc -= optind, argv += optind;
    assert(argc == 1);
    const char *arrayName = argv[0];
    struct stat st;
    int rc = fstat(fileno(stdin), &st);
    assert(rc == 0);
    size_t arraySize = st.st_size;
    assert(arraySize > 0);
    assert(arraySize <= (1 << 20));
    unsigned char buf[arraySize+1];
    size_t ret = fread(buf, 1, arraySize+1, stdin);
    assert(ret == arraySize);
    if (compress) {
	int zsize = LZ4_COMPRESSBOUND(arraySize);
	unsigned char zbuf[zsize];
	zsize = LZ4_compress_HC((void *) buf, (void *) zbuf, arraySize, zsize, LZ4HC_CLEVEL_MAX);
	assert(zsize > 0);
	assert(frameSize + zsize < arraySize);
	if (frameSize) {
	    unsigned frame[] = { htole32(0x184D2A56), htole32(zsize) };
	    assert(frameSize == sizeof frame);
	    memcpy(buf, frame, frameSize);
	}
	memcpy(buf + frameSize, zbuf, zsize);
	arraySize = frameSize + zsize;
    }
    unsigned char *p = buf;
    unsigned char *end = buf + arraySize;
    *end = '\0'; // the sentinel, non-printable
    printf("static const char %s[%zu] = {\n", arrayName, arraySize);
    putchar('"');
    unsigned char c = *p++;
    if (!xisprint(c))
	goto nonprintable;
    while (1) {
	// Printable characters...
	int questionMarks = 0;
	while (1) {
	    switch (c) {
	    case '?':
		// Quote successive question marks starting with
		// the second one, to avoid trigraphs.
		if (questionMarks++)
		    putchar('\\');
		putchar('?');
		break;
	    case '"': case '\\':
		putchar('\\');
		// fall through
	    default:
		questionMarks = 0;
		putchar(c);
	    }
	    c = *p++;
	    if (!xisprint(c))
		break;
	}
	// Reached the sentinel, p points past the sentinel?
	if (p > end)
	    break;
nonprintable:
	// followed by non-printable characters.
	while (1) {
	    // If the last input byte is a zero byte, identify it with the
	    // trailing zero byte which comes on behalf of this string literal.
	    // Then the arraySize and the literal size will match exactly.
	    if (p == end && c == '\0') {
		p++;
		break;
	    }
	    switch (c) {
	    // These escape characters provide the shortest codes;
	    // '\a' is excluded because it can be encoded with '\7'.
	    case '\b': putchar('\\'), putchar('b'); break;
	    case '\f': putchar('\\'), putchar('f'); break;
	    case '\n': putchar('\\'), putchar('n'); break;
	    case '\r': putchar('\\'), putchar('r'); break;
	    case '\t': putchar('\\'), putchar('t'); break;
	    case '\v': putchar('\\'), putchar('v'); break;
	    // Otherwise, octal codes are the shortest; unlike
	    // hex codes, they also use the smallest alphabet.
	    // Note that an octal code can have one, two, or three
	    // digits, but there is no ambiguity, since the next
	    // character cannot be a printable digit.
	    default: printf("\\%o", c);
	    }
	    c = *p++;
	    if (p > end)
		break;
	    if (xisprint(c))
		break;
	}
	if (p > end)
	    break;
	putchar('"'), putchar('\n');
	putchar('"');
    }
    putchar('"'), putchar('\n');
    printf("};\n");
    assert(fflush(stdout) == 0);
    return 0;
}
