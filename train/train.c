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

// This program creates a dictionary for rpm headers; it uses the new COVER
// algorithm recently implemented in zstd.  The headers are read from stdin,
// there is no need to split them into separate files.

#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <unistd.h>
#include <arpa/inet.h>

struct samples {
    unsigned nbSamples;
    size_t samplesSizes[1<<20];
    char samplesBuffer[1<<30];
} samples;

// Maximum bytes in a header permitted by rpm, without magic.
#define headerMaxSize (32 << 20)

static long headerDataSize(char *blob)
{
    static unsigned char magic[8] = {
	0x8e, 0xad, 0xe8, 0x01, 0x00, 0x00, 0x00, 0x00
    };
    assert(memcmp(blob, magic, sizeof magic) == 0);
    unsigned *ei = (unsigned *) (blob + 8);
    unsigned il = ntohl(ei[0]);
    unsigned dl = ntohl(ei[1]);
    assert(!(il > headerMaxSize / 16 || dl > headerMaxSize));
    size_t dataSize = 16 * il + dl;
    assert(dataSize);
    assert(!(8 + dataSize > headerMaxSize));
    return dataSize;
}

// LZ4 uses 64K window, and thus a 64K dictionary is a bit too big.
// On the other hand, a full 64K dictionary might still be preferrable,
// because LZ4 can omit some bound checking in this case.
#define maxDictSize (64<<10)
#define maxSampleSize (32<<10)

static void load(void)
{
    char *buf = samples.samplesBuffer;
    const char *end = buf + sizeof samples.samplesBuffer;
    // Combines two headers into a single sample, much like zhdlist.
    while (1) {
	char lead[16];
	// Peak at the first header.
	size_t ret = fread(lead, 1, 16, stdin);
	if (ret == 0)
	    break;
	assert(ret == 16);
	// Place only the sizes, no magic.
	char *p = buf;
	assert(p + 8 < end);
	memcpy(p, lead + 8, 8);
	p += 8;
	// Read the first header's data.
	long dataSize = headerDataSize(lead);
	assert(p + dataSize <= end);
	ret = fread(p, 1, dataSize, stdin);
	assert(ret == dataSize);
	p += dataSize;
	// Peak at the second header.
	ret = fread(lead, 1, 16, stdin);
	if (ret) {
	    assert(ret == 16);
	    // Place only the sizes, no magic.
	    assert(p + 8 < end);
	    memcpy(p, lead + 8, 8);
	    p += 8;
	    // Read the second header's data.
	    dataSize = headerDataSize(lead);
	    assert(p + dataSize <= end);
	    ret = fread(p, 1, dataSize, stdin);
	    assert(ret == dataSize);
	    p += dataSize;
	}
	size_t fill = p - buf;
	if (fill > maxSampleSize)
	    fill = maxSampleSize;
	buf += fill;
	samples.samplesSizes[samples.nbSamples++] = fill;
	if (ret == 0)
	    break;
#if 0	// Ain't got time to die, the whole thing takes 2 hours.
	if (samples.nbSamples > 999)
	    return;
#endif
    }
    assert(!ferror(stdin));
    assert(samples.nbSamples);
}

#define ZDICT_STATIC_LINKING_ONLY
#include "zdict.h"

int main(int argc, char **argv)
{
    assert(argc == 1);
    assert(!isatty(0));
    assert(!isatty(1));
    load();
    COVER_params_t params = {
	.nbThreads = 2,
	.notificationLevel = 3,
#if 0	// Ain't got time to die.
	.k = 256, .d = 8,
#endif
    };
    char dict[maxDictSize];
    size_t dictSize = COVER_optimizeTrainFromBuffer(dict, sizeof dict,
	    samples.samplesBuffer, samples.samplesSizes, samples.nbSamples,
	    &params);
    assert(dictSize == sizeof dict);
    fprintf(stderr, "best parameters: k=%u d=%u\n", params.k, params.d);
    size_t ret = fwrite(dict, 1, dictSize, stdout);
    assert(ret == dictSize);
    assert(fflush(stdout) == 0);
    return 0;
}
