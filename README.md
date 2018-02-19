## File format

The zpkglist file format is based on
[LZ4 skippable frames](https://github.com/lz4/lz4/blob/dev/doc/lz4_Frame_format.md#skippable-frames).
It is otherwise not compatible with the LZ4 file format, and requires a special
decoding routine (LZ4 decompression will only produce an empty output).

A valid zpkglist file starts with the leading frame, followed by the dictionary
frame, followed by data frames.  There is no trailing checksum.

### The leading frame
```
magic 0x184D2A55 | size = 16 | total uncompressed size | buf1 size | jbuf size
   (4 bytes)     | (4 bytes) |        (8 bytes)        | (4 bytes) | (4 bytes)
```
The `total uncompressed size` field allows the following fast check to be
performed by APT: if the size has changed, there is no need to calculate
the MD5 checksum of the data.  The last two fields assist optimal memory
allocation in the decoder (see below).

### The dictionary frame
```
magic 0x184D2A56 | compressed size |  compressed data
   (4 bytes)     |    (4 bytes)    | (compressed size bytes)
```
The dictionary is compressed with LZ4.
The uncompressed size of the dictionary is 65,536 bytes.
Using exactly this size enables important optimizations in the LZ4 library.

In a file with no data frames (`total uncompressed size = 0`), there should be
no dictionary.  To decode data frames, though, the dictionary must be present.

### Data frames
```
magic 0x184D2A57 | compressed size + 4 | uncompressed size |  compressed data
   (4 bytes)     |    (4 bytes)        |     (4 bytes)     | (compressed size - 4 bytes)
```
Frame data is compressed with LZ4, normally using the dictionary (see below).

The compressed size field takes into account the uncompressed size field;
technically, the latter comes on behalf of the skippable frame's payload.

### RPM header blobs
```
 <il,dl>  |   header data    || rpm header magic |  <il,dl>  |   header data
(8 bytes) | (16*il+dl bytes) ||    (8 bytes)     | (8 bytes) | (16*il+dl bytes)
```
Each uncompressed data frame contains 1, 2, 3, or 4
[RPM header blobs](http://ftp.rpm.org/max-rpm/s1-rpm-file-format-rpm-file-format.html#S3-RPM-FILE-FORMAT-HEADER-STRUCTURE).
There is no leading magic with the first header; it can be easily restored
upon the decoding with a special buffer management technique.  Subsequent
headers start with the magic, to facilitate fast recovery of original data.

All 4-byte and 8-byte integers (the latter being the `total uncompressed size`
field) are stored in little-endian byte order (as in LZ4 skippable frames).
The `il` and `dl` sizes are stored in network byte order; they are specific
to RPM, not to zpkglist.

### Normal frames and jumbo frames

There are further two kinds of data frames: normal frames and jumbo frames.
A frame is a jumbo frame if and only if it contains a single RPM header
whose `uncompressed size` is greater than 128K.  Otherwise, the frame
is a normal one: its data size is less than or equal to `128<<10` bytes,
and it can hold more than one header blob.  (This implies that big headers
cannot be combined within a single frame.  The classification is based
solely on the `uncompressed size` field.)

Normal frames are compressed using the dictionary, and can only be decoded
with the dictionary.  Jumbo frames are compressed without the dictionary,
and need no dictionary for decompression.  (This can help reduce copying:
a jumbo frame can be decompressed to and the blob returned in a newly malloc'd
chunk, while decoding normal frames may require a somewhat complex memory
arrangement of the dictionary and the output buffer.)

The decoder is expected to decode normal frames without extra malloc calls.
To assist this process, the leading frame provides the `buf1 size` field,
which must be set to either of the following, whichever is greater:
* `compressed size` from the dictionary frame;
* maximum `compressed size + uncompressed size` among the normal frames;
* maximum `compressed size` among the jumbo frames.

Jumbo frames can be decoded without repeated malloc/realloc calls as well:
the `jbuf size` provides the maximum `uncompressed size` among the jumbo frames.
