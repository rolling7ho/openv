# Load a game image dump (tools/dump/openv-dump.exe output) and find its executable sections from the PE
# headers, so no script hardcodes section ranges for one game build.
# The dump path comes from $OPENV_DUMP, defaulting to the 1158.16 research dump.
import os, struct

BASE = 0x140000000
PATH = os.environ.get('OPENV_DUMP', 'research/dumps/1158.16-a.bin')


def text_sections(dump):
    pe = struct.unpack_from('<I', dump, 0x3c)[0]
    count, opt = struct.unpack_from('<H', dump, pe + 6)[0], struct.unpack_from('<H', dump, pe + 20)[0]
    out = []
    for i in range(count):
        s = pe + 24 + opt + 40 * i
        vsize, va = struct.unpack_from('<II', dump, s + 8)
        if struct.unpack_from('<I', dump, s + 36)[0] & 0x20000000:   # IMAGE_SCN_MEM_EXECUTE
            out.append((va, va + vsize))
    return out


def data_sections(dump):
    pe = struct.unpack_from('<I', dump, 0x3c)[0]
    count, opt = struct.unpack_from('<H', dump, pe + 6)[0], struct.unpack_from('<H', dump, pe + 20)[0]
    out = []
    for i in range(count):
        s = pe + 24 + opt + 40 * i
        vsize, va = struct.unpack_from('<II', dump, s + 8)
        if struct.unpack_from('<I', dump, s + 36)[0] & 0x80000000:   # IMAGE_SCN_MEM_WRITE
            out.append((va, va + vsize))
    return out


def file_version(dump):
    """FileVersion from the VS_FIXEDFILEINFO in the image's resources, as (major, minor, build, minor2)."""
    i = dump.find(struct.pack('<I', 0xFEEF04BD))
    if i < 0: return None
    ms, ls = struct.unpack_from('<II', dump, i + 8)
    return ms >> 16, ms & 0xffff, ls >> 16, ls & 0xffff


dump = open(PATH, 'rb').read() if os.path.exists(PATH) else b''
TEXT = text_sections(dump) if dump else []
