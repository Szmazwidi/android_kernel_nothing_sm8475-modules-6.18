#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-only
"""Read the SELinux label of every path in an EROFS image (read-only, no mounting).

Used by build_images.py to give the rebuilt vendor_dlkm exactly the labels of the stock
image. Only uncompressed directories are needed (EROFS never compresses directories).

    erofs_labels.py vendor_dlkm.img [mount_point]   # prints a file_contexts file
"""
import re
import struct
import sys

EROFS_MAGIC = 0xE0F5E1E2
SECURITY_INDEX = 6
FLAT_PLAIN, FLAT_INLINE = 0, 2
S_IFDIR = 0o040000


class Erofs:
    def __init__(self, data):
        self.d = data
        sb = data[1024:1024 + 128]
        magic, _, _, blkszbits, _, root_nid = struct.unpack_from('<IIIBBH', sb, 0)
        if magic != EROFS_MAGIC:
            raise ValueError('not an EROFS image')
        self.bs = 1 << blkszbits
        self.root = root_nid
        self.meta, self.xattr_blk = struct.unpack_from('<II', sb, 40)

    def inode(self, nid):
        off = self.meta * self.bs + nid * 32
        fmt, icount, mode = struct.unpack_from('<HHH', self.d, off)
        if fmt & 1:  # extended
            size, = struct.unpack_from('<Q', self.d, off + 8)
            raw, = struct.unpack_from('<I', self.d, off + 16)
            isize = 64
        else:
            size, = struct.unpack_from('<I', self.d, off + 8)
            raw, = struct.unpack_from('<I', self.d, off + 16)
            isize = 32
        xsize = 0 if icount == 0 else 12 + (icount - 1) * 4
        return dict(off=off, isize=isize, xsize=xsize, mode=mode, size=size, raw=raw,
                    layout=(fmt >> 1) & 7)

    def _entry(self, pos):
        nlen, idx, vsize = struct.unpack_from('<BBH', self.d, pos)
        name = self.d[pos + 4:pos + 4 + nlen]
        value = self.d[pos + 4 + nlen:pos + 4 + nlen + vsize]
        return idx, name, value, (4 + nlen + vsize + 3) & ~3

    def label(self, ino):
        if not ino['xsize']:
            return None
        pos = ino['off'] + ino['isize']
        shared = self.d[pos + 4]
        end = pos + ino['xsize']
        ids = struct.unpack_from('<%dI' % shared, self.d, pos + 12)
        pos += 12 + 4 * shared
        found = []
        for i in ids:
            found.append(self._entry(self.xattr_blk * self.bs + i * 4)[:3])
        while pos < end:
            idx, name, value, n = self._entry(pos)
            found.append((idx, name, value))
            pos += n
        for idx, name, value in found:
            if idx == SECURITY_INDEX and name == b'selinux':
                return value.rstrip(b'\0').decode()
        return None

    def _dir_data(self, ino):
        if ino['layout'] == FLAT_PLAIN:
            start = ino['raw'] * self.bs
            return self.d[start:start + ino['size']]
        if ino['layout'] == FLAT_INLINE:
            full = (ino['size'] // self.bs) * self.bs
            start = ino['raw'] * self.bs
            tail = ino['off'] + ino['isize'] + ino['xsize']
            return self.d[start:start + full] + self.d[tail:tail + ino['size'] - full]
        raise ValueError('unsupported directory layout %d' % ino['layout'])

    def listdir(self, ino):
        data = self._dir_data(ino)
        for b in range(0, len(data), self.bs):
            blk = data[b:b + self.bs]
            first_off, = struct.unpack_from('<H', blk, 8)
            n = first_off // 12
            ents = [struct.unpack_from('<QHBB', blk, i * 12) for i in range(n)]
            for i, (nid, nameoff, _, _) in enumerate(ents):
                end = ents[i + 1][1] if i + 1 < n else len(blk)
                name = blk[nameoff:end].split(b'\0', 1)[0].decode()
                if name not in ('.', '..'):
                    yield name, nid

    def walk(self):
        stack = [('', self.root)]
        while stack:
            path, nid = stack.pop()
            ino = self.inode(nid)
            yield path or '/', ino
            if ino['mode'] & 0o170000 == S_IFDIR:
                for name, child in self.listdir(ino):
                    stack.append((path + '/' + name, child))


def labels(image_path):
    fs = Erofs(open(image_path, 'rb').read())
    return {p: (fs.label(ino), ino['mode']) for p, ino in fs.walk()}


def file_contexts(lbl, mount_point):
    out = []
    for p in sorted(lbl):
        ctx = lbl[p][0]
        if ctx:
            full = '/' + mount_point.strip('/') + ('' if p == '/' else p)
            out.append('%s %s' % (re.sub(r'([.+*?^$()\[\]{}|\\])', r'\\\1', full), ctx))
    return '\n'.join(out) + '\n'


if __name__ == '__main__':
    mp = sys.argv[2] if len(sys.argv) > 2 else 'vendor_dlkm'
    sys.stdout.write(file_contexts(labels(sys.argv[1]), mp))
