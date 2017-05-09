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

#ifndef ZPKGLIST_H
#define ZPKGLIST_H

#ifdef __cplusplus
extern "C" {
#else
#include <stdbool.h>
#endif

// Compress a list of rpm headers, such as produced by genpkglist,
// into zpkglist file format (see README.md).  Returns 1 on success,
// 0 on empty input (with valid output still written), -1 on error.
// Information about an error is returned via the err[2] parameter:
// the first string is typically a function name, and the second is
// a string which describes the error.  Both strings normally come
// from the read-only data section.
int zpkglistCompress(int in, int out, const char *err[2],
		     void (*hash)(void *buf, unsigned size, void *arg), void *arg)
		     __attribute__((nonnull(3)));

#ifdef __cplusplus
}
#endif

#endif
