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

// This program converts a blob into a C header.
// Usage: blob2inc arrayName <blob >hdr.h

#include <stdio.h>
#include <assert.h>
#include <sys/stat.h>

// ASCII range from ' ' to '~'.
#define xisprint(c) ((unsigned)(c)-' ' <= '~'-' ')

int main(int argc, char **argv)
{
    assert(argc == 2);
    struct stat st;
    int rc = fstat(fileno(stdin), &st);
    assert(rc == 0);
    assert(st.st_size > 0);
    assert(st.st_size <= (1 << 20));
    unsigned char buf[st.st_size+1];
    size_t ret = fread(buf, 1, st.st_size+1, stdin);
    assert(ret == st.st_size);
    unsigned char *end = buf + st.st_size;
    *end = '\0';
    printf("static const char %s[%d] = {\n", argv[1], (int) st.st_size);
    unsigned char *p = buf;
    unsigned char c = *p++;
    putchar('"');
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
nonprintable:
	// followed by non-printable characters.
	while (p < end) {
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
	    if (xisprint(c))
		break;
	}
	putchar('"'), putchar('\n');
	if (p == end)
	    break;
	putchar('"');
    }
    printf("};\n");
    assert(fflush(stdout) == 0);
    return 0;
}
