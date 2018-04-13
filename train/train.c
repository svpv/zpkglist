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

// This program creates a dictionary for rpm headers; it uses the new COVER
// algorithm recently implemented in zstd.  The headers are read from stdin,
// there is no need to split them into separate files.

#include <stdio.h>
#include <stdbool.h>
#include <string.h>
#include <assert.h>
#include <unistd.h>
#include <arpa/inet.h>
#include "../header.h"

struct samples {
    size_t nbSamples;
    size_t samplesSizes[1<<20];
    char samplesBuffer[1<<30];
};

static struct samples samples;
#define maxSampleSize (32<<10)

static void load(void)
{
    char lead[16];

    // Peak at the first header.
    size_t ret = fread(lead, 1, 16, stdin);
    assert(ret == 16);
    assert(headerCheckMagic(lead));

    // The size of the header's data after (il,dl).
    size_t dataSize = headerDataSize(lead);
    assert(dataSize > 0);

    bool eof = false;
    int nbFrames = 0;
    char *buf = samples.samplesBuffer;
    const char *end = buf + sizeof samples.samplesBuffer;

    do {
	char *cur = buf;
	// Trying to fit four headers into 128K.
	for (int i = 0; i < 4; i++) {
	    // Put this header's leading bytes.
	    // The very first magic won't be written.
	    if (i == 0) {
		memcpy(cur, lead + 8, 8);
		cur += 8;
	    }
	    else {
		memcpy(cur, lead, 16);
		cur += 16;
	    }
	    // Read this header's data + the next header's leading bytes.
	    assert(cur + dataSize + 16 < end);
	    ret = fread(cur, 1, dataSize + 16, stdin);
	    cur += dataSize;
	    if (ret == dataSize) {
		eof = true;
		break;
	    }
	    assert(ret == dataSize + 16);
	    // Save the leading bytes of the next header for the next iteration -
	    // either in this "for i" loop, or in the outer "while 1" loop.
	    // Much the same logic as in compress.c.
	    memcpy(lead, cur, 16);
	    assert(headerCheckMagic(lead));
	    dataSize = headerDataSize(lead);
	    assert(dataSize > 0);
	    // Does the next header still fit in?
	    if ((cur - buf) + (16 + dataSize) > (128 << 10))
		break;
	}
	if (++nbFrames % 3) // frame sampling
	    continue;
	size_t fill = cur - buf;
	if (fill > maxSampleSize)
	    fill = maxSampleSize;
	buf += fill;
	samples.samplesSizes[samples.nbSamples++] = fill;
    } while (!eof);
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
    ZDICT_cover_params_t params = {
	.nbThreads = 2,
	.zParams.notificationLevel = 3,
	.zParams.compressionLevel = 1,
    };
    char dict[64<<10];
    size_t dictSize = ZDICT_optimizeTrainFromBuffer_cover(dict, sizeof dict,
	    samples.samplesBuffer, samples.samplesSizes, samples.nbSamples,
	    &params);
    assert(dictSize == sizeof dict);
    fprintf(stderr, "best parameters: k=%u d=%u\n", params.k, params.d);
    size_t ret = fwrite(dict, 1, dictSize, stdout);
    assert(ret == dictSize);
    assert(fflush(stdout) == 0);
    return 0;
}
