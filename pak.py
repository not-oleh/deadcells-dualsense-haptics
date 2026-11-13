"""Read files from Dead Cells res.pak (Heaps PAK v1)."""
import struct

def entries(path):
    f = open(path, "rb"); hdr = f.read(12)
    assert hdr[:3] == b"PAK"
    hsize = struct.unpack_from("<i", hdr, 4)[0]
    f.seek(0); b = f.read(hsize); p = 12 + 64  # 64-char stamp
    out = {}
    def node(prefix):
        nonlocal p
        n = b[p]; name = b[p + 1:p + 1 + n].decode(); p += 1 + n
        flags = b[p]; p += 1
        full = prefix + name
        if flags & 1:
            cnt = struct.unpack_from("<i", b, p)[0]; p += 4
            for _ in range(cnt): node(full + "/" if full else "")
        else:
            if flags & 2: pos = struct.unpack_from("<d", b, p)[0]; p += 8
            else: pos = struct.unpack_from("<I", b, p)[0]; p += 4
            size = struct.unpack_from("<i", b, p)[0]; p += 8
            out[full] = (hsize + int(pos), size)
    node("")
    return out

def read(path, name):
    off, size = entries(path)[name]
    with open(path, "rb") as f:
        f.seek(off); return f.read(size)
