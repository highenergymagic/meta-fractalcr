import errno
import gzip
import os
import struct
import sys

# Physical extents vary with the host filesystem and allocation history.
# Fixed nonzero pages give identical archives for sparse and allocated zeros.
PAGE = 4096
ZERO_PAGE = bytes(PAGE)

with open(sys.argv[1], 'rb') as source, open(sys.argv[2], 'wb') as target:
    size = os.fstat(source.fileno()).st_size
    with gzip.GzipFile(filename='', mode='wb', fileobj=target, mtime=0) as out:
        out.write(b'FCRBLK01' + struct.pack('<Q', size))
        offset = 0
        written_end = 0
        while offset < size:
            try:
                start = os.lseek(source.fileno(), offset, os.SEEK_DATA)
            except OSError as error:
                if error.errno != errno.ENXIO:
                    raise
                break
            end = min(os.lseek(source.fileno(), start, os.SEEK_HOLE), size)
            page_start = max(start // PAGE * PAGE, written_end)
            page_end = min((end + PAGE - 1) // PAGE * PAGE, size)
            source.seek(page_start)
            while page_start < page_end:
                length = min(PAGE, size - page_start)
                data = source.read(length)
                if len(data) != length:
                    raise ValueError('Truncated seed')
                if data != ZERO_PAGE[:length]:
                    out.write(struct.pack('<QQ', page_start, length))
                    out.write(data)
                page_start += length
            written_end = max(written_end, page_end)
            offset = end
        out.write(bytes(16))
