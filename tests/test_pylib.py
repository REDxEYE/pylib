"""Regression tests for pylib.

Run from a build directory that contains ``pylib.abi3.so`` (or ``.pyd``)::

    PYTHONPATH=cmake-build-release python -m unittest discover -s tests -v

Every test here pins a bug that used to crash, corrupt memory or return wrong
data (see AUDIT.md for the numbered findings).
"""
import gc
import os
import random
import struct
import tempfile
import unittest

import pylib
from pylib.compression import (LZ4ChainDecoder, LZ4ChainEncoder, lz4_compress, lz4_decompress,
                               zstd_compress, zstd_compress_stream, zstd_decompress,
                               zstd_decompress_stream)
from pylib.image import decode_texture, encode_exr, encode_png
from pylib.mesh import SMDModel, SMDNode, decode_index_buffer, decode_vertex_buffer
from pylib.vtf import ImageFormat, VTFFile

RNG = random.Random(1234)


def rand(n):
    return RNG.randbytes(n)


class ModuleTests(unittest.TestCase):
    def test_package_path_survives_submodule_init(self):  # 1
        gc.collect()
        self.assertEqual(pylib.__path__, [])
        __import__('pylib.vtf')  # package import must still resolve

    def test_type_error_raises(self):  # 9
        with self.assertRaises(TypeError):
            zstd_decompress(123, 10)


class ZstdTests(unittest.TestCase):
    def test_stream_roundtrip_sizes(self):  # 13
        for n in (0, 1, 65535, 65536, 65537, 200000):
            for data in (b'a' * n, rand(n)):
                for compress in (zstd_compress, zstd_compress_stream):
                    self.assertEqual(zstd_decompress_stream(compress(data, 19)), data, (compress, n))

    def test_stream_concatenated_frames(self):  # 13
        self.assertEqual(zstd_decompress_stream(zstd_compress(b'hello') + zstd_compress(b'world')),
                         b'helloworld')

    def test_stream_truncated_raises(self):  # 13
        with self.assertRaises(ValueError):
            zstd_decompress_stream(zstd_compress(rand(100000))[:-1000])

    def test_one_shot_roundtrip(self):  # 15
        for data in (b'', b'x', rand(70000)):
            self.assertEqual(zstd_decompress(zstd_compress(data), len(data)), data)
            self.assertEqual(lz4_decompress(lz4_compress(data), len(data)), data)


class LZ4ChainTests(unittest.TestCase):
    def test_block_larger_than_buffer_rejected(self):  # 25
        with self.assertRaises(ValueError):
            LZ4ChainDecoder(4096, 0).decompress(lz4_compress(b'a' * 4_000_000), 4_000_000)

    def test_buffer_size_overflow_rejected(self):  # 26
        for cls in (LZ4ChainDecoder, LZ4ChainEncoder):
            with self.assertRaises(ValueError):
                cls(1 << 20, 4096)

    def test_dependent_blocks_roundtrip(self):  # 32
        base = rand(20000)
        blocks = [base[:8000], base[2000:14000] + base[:4000], base[5000:20000], base * 2]
        enc, dec = LZ4ChainEncoder(65536, 2), LZ4ChainDecoder(65536, 2)
        for block in blocks:
            self.assertEqual(dec.decompress(enc.compress(block), len(block)), block)

    def test_long_stream_through_buffer_compaction(self):  # 32
        # Enough blocks to wrap the encoder/decoder buffers several times, so the
        # compaction + dictionary reload path is exercised on both sides.
        base = rand(30000)
        enc, dec = LZ4ChainEncoder(16384, 1), LZ4ChainDecoder(16384, 1)
        for i in range(200):
            start = (i * 7919) % 20000
            block = base[start:start + 4000 + i * 37 % 12000]
            self.assertEqual(dec.decompress(enc.compress(block), len(block)), block, i)

    def test_uninitialized_raises(self):  # 27
        with self.assertRaises(RuntimeError):
            LZ4ChainDecoder.__new__(LZ4ChainDecoder).decompress(b'\x10a', 1)
        with self.assertRaises(RuntimeError):
            LZ4ChainEncoder.__new__(LZ4ChainEncoder).compress(b'abc')

    def test_reinit(self):  # 28
        dec = LZ4ChainDecoder(65536, 1)
        dec.__init__(4096, 0)
        data = b'abc' * 1000
        self.assertEqual(dec.decompress(lz4_compress(data), len(data)), data)


class MeshoptTests(unittest.TestCase):
    def test_corrupt_input_raises(self):  # 34
        with self.assertRaises(ValueError):
            decode_index_buffer(b'\x00' * 64, 4, 3)
        with self.assertRaises(ValueError):
            decode_vertex_buffer(b'\x00' * 64, 16, 4)

    def test_preconditions(self):  # 35
        with self.assertRaises(ValueError):
            decode_vertex_buffer(b'\xa0' + b'\x00' * 600, 512, 1)
        with self.assertRaises(ValueError):
            decode_vertex_buffer(b'\xa0' + b'\x00' * 64, 6, 1)
        with self.assertRaises(ValueError):
            decode_index_buffer(b'\xe1' + b'\x00' * 64, 2, 4)


SMD = b'''version 1
nodes
0 "root" -1
1 "caf\xe9" 0
end
skeleton
time 0
0 0 0 0 0 0 0
1 +1 .5 1e-1 -0.5 0 0
end
triangles
m\xe4t
0 0 0 0 0 0 1 0 0 2 0 0.5 1 0.5
0 1 0 0 0 0 1 1 0
0 0 1 0 0 0 1 0 1
end
'''


class SMDTests(unittest.TestCase):
    def test_children_accessible(self):  # 38, 49, 53
        m = SMDModel(SMD)
        self.assertEqual([n.name for n in m.nodes], ['root', 'caf\xe9'])
        self.assertEqual([round(c, 6) for c in m.skeleton.frames[0][1].pos], [1.0, 0.5, 0.1])
        tri = m.triangles[0]
        self.assertEqual(tri.material, 'm\xe4t')
        self.assertEqual(tri.vertices[0].weights, [(0, 0.5), (1, 0.5)])
        repr(m), repr(m.nodes[1]), repr(tri)

    def test_child_types_not_instantiable(self):  # 39
        with self.assertRaises(TypeError):
            SMDNode()

    def test_missing_version_raises(self):  # 54
        with self.assertRaises(ValueError):
            SMDModel(b'nodes\n0 "root" -1\nend\n')

    def test_vertex_animation_exposed(self):  # 57
        m = SMDModel(b'version 1\nvertexanimation\ntime 0\n0 1 2 3 0 0 1\n1 4 5 6 0 1 0\n'
                     b'time 1\n0 1.5 2 3 0 0 1\nend\n')
        self.assertEqual(m.kind, 'vertex_animation')
        self.assertEqual(m.vertex_animation, {
            0: [(0, (1.0, 2.0, 3.0), (0.0, 0.0, 1.0)), (1, (4.0, 5.0, 6.0), (0.0, 1.0, 0.0))],
            1: [(0, (1.5, 2.0, 3.0), (0.0, 0.0, 1.0))],
        })

    def test_malformed_input_raises(self):  # 40, 41, 50, 51, 53
        bad = [b'version abc\n', 'version 1\nnodes\n0 "\udc80" -1\nend\n',
               b'version 1\ntriangles\nmat\n0 0 0 0 0 0 0 0 0 -5\n',
               b'version 1\ntriangles\nmat\n0 0 0 0 0 0 0 0 0 1000000000\n',
               b'version\n', b'version 1\nskeleton\ntime\n', b'version 1\nskeleton\ntime 0\n0 1 2\n',
               b'version 1\ntriangles\nmat\n0 1 2\n', b'version 1\ntriangles\nmat\n',
               b'version 12abc\n']
        for data in bad:
            with self.assertRaises((ValueError, UnicodeEncodeError), msg=repr(data)):
                SMDModel(data)

    def test_rejects_keywords(self):  # 43
        with self.assertRaises(TypeError):
            SMDModel(SMD, foo=1)


def crop(full, w, h, full_w, px):
    return b''.join(full[y * full_w * px:(y * full_w + w) * px] for y in range(h))


class TextureTests(unittest.TestCase):
    def test_input_size_covers_partial_blocks(self):  # 58, 59
        for args in ((b'', 2, 2, 'BC1'), (b'\0' * 8, 6, 6, 'BC1'), (b'\0' * 8, 64, 64, 'ETC1'),
                     (b'\0' * 8, 64, 64, 'ETC2'), (b'', 1, 1, 'BC7')):
            with self.assertRaises(ValueError, msg=repr(args[1:])):
                decode_texture(*args)
        self.assertEqual(len(decode_texture(b'\0' * 8, 2, 2, 'BC1')), 16)

    def test_partial_blocks_match_cropped_full_decode(self):  # 60, 61
        for fmt, block, px in (('BC1', 8, 4), ('BC3', 16, 4), ('BC7', 16, 4), ('BC4', 8, 1),
                               ('BC5', 16, 2), ('BC6H', 16, 6)):
            blocks = rand(block * 9)
            full = decode_texture(blocks, 12, 12, fmt)
            self.assertEqual(decode_texture(blocks, 10, 9, fmt), crop(full, 10, 9, 12, px), fmt)
            one = blocks[:block]
            full = decode_texture(one, 4, 4, fmt)
            for w, h in ((1, 1), (2, 2), (3, 1), (1, 3)):
                self.assertEqual(decode_texture(one, w, h, fmt), crop(full, w, h, 4, px), (fmt, w, h))

    def test_signed_formats(self):  # 64
        block = rand(16)
        for fmt, px in (('BC4S', 1), ('BC5S', 2), ('BC6H_SF16', 6)):
            self.assertEqual(len(decode_texture(block, 4, 4, fmt)), 16 * px)
        self.assertEqual(decode_texture(block, 4, 4, 'BC6H_UF16'), decode_texture(block, 4, 4, 'BC6H'))


class EncodeTests(unittest.TestCase):
    def test_png_rejects_int_overflow(self):  # 65
        with self.assertRaises((ValueError, OverflowError)):
            encode_png(b'', 40000, 40000, 4)

    def test_exr_channels_sorted(self):  # 66
        exr = encode_exr(struct.pack('<64f', *range(64)), 4, 4, 4)
        tag = b'channels\x00chlist\x00'
        i = exr.index(tag) + len(tag) + 4
        names = []
        while exr[i] != 0:
            j = exr.index(b'\x00', i)
            names.append(exr[i:j].decode())
            i = j + 1 + 16
        self.assertEqual(names, ['A', 'B', 'G', 'R'])


class ETCTests(unittest.TestCase):
    def test_etc2_eac_color_matches_etc2(self):  # 72
        for _ in range(20):
            alpha, color = rand(8), rand(8)
            rgba = decode_texture(alpha + color, 4, 4, 'ETC2_EAC')
            rgb = decode_texture(color, 4, 4, 'ETC2')
            self.assertEqual([rgba[i] for i in range(64) if i % 4 != 3],
                             [rgb[i] for i in range(64) if i % 4 != 3])

    def test_new_formats_sizes(self):  # 72
        for fmt, block in (('ETC2_A1', 8), ('ETC2_RGBA', 16), ('EAC_R11', 8), ('EAC_R11S', 8),
                           ('EAC_RG11', 16), ('EAC_RG11S', 16)):
            self.assertEqual(len(decode_texture(rand(block * 4), 6, 6, fmt)), 6 * 6 * 4, fmt)
            with self.assertRaises(ValueError):
                decode_texture(rand(block * 3), 6, 6, fmt)

    def test_eac_r11_channel_layout(self):  # 72
        out = decode_texture(rand(8), 4, 4, 'EAC_R11')
        self.assertEqual({(out[i + 1], out[i + 2], out[i + 3]) for i in range(0, 64, 4)}, {(0, 0, 255)})


class VTFTests(unittest.TestCase):
    def test_generate_mipmaps(self):  # 73, 74, 75
        v = VTFFile()
        v.create(16, 16)
        v.set_data(0, 0, 0, 0, bytes([200, 100, 50, 255]) * 256)
        v.generate_mipmaps()
        self.assertEqual(v.get_data(0, 0, 0, 1), bytes([200, 100, 50, 255]) * 64)
        f = VTFFile()
        f.create(8, 8, 1, 1, 1, ImageFormat.RGBA32323232F)
        f.generate_mipmaps()

    def test_generate_mipmaps_without_mips_keeps_base(self):  # 75
        v = VTFFile()
        v.create(16, 16, 1, 1, 1, ImageFormat.RGBA8888, True, False)
        v.set_data(0, 0, 0, 0, bytes(range(256)) * 4)
        before = v.get_data(0, 0, 0, 0)
        v.generate_mipmaps()
        self.assertEqual(v.get_data(0, 0, 0, 0), before)

    def test_level_sizes(self):  # 76, 77
        v = VTFFile()
        v.create(64, 64)
        self.assertEqual(len(v.get_data(0, 0, 0, 1)), 32 * 32 * 4)
        with self.assertRaises(ValueError):
            v.set_data(0, 0, 0, 1, b'\0' * 1024)
        v.set_data(0, 0, 0, 1, b'\x07' * 4096)
        self.assertEqual(v.get_data(0, 0, 0, 1), b'\x07' * 4096)
        with self.assertRaises(IndexError):
            v.get_data(0, 0, 0, 99)

    def test_argument_errors(self):  # 78, 80
        v = VTFFile()
        v.create(16, 16)
        with self.assertRaises(TypeError):
            v.set_flag('x', True)
        with self.assertRaises(TypeError):
            v.create(64, 64, 'a')
        with self.assertRaises((OverflowError, ValueError)):
            v.create(-1, 16)
        with self.assertRaises(ValueError):
            VTFFile().create_from_data(b'\0' * 64, 4, 4, resize_to_pow2=99)


def vpk_v2(entries, data):
    tree = b''
    for ext, path, name, offset, size in entries:
        tree += ext + b'\0' + path + b'\0' + name + b'\0'
        tree += struct.pack('<IHHIIH', 0, 0, 0x7fff, offset, size, 0xffff) + b'\0' + b'\0'
    tree += b'\0'
    return struct.pack('<IIIIIII', 0x55AA1234, 2, len(tree), len(data), 0, 0, 0) + tree + data


class VPKTests(unittest.TestCase):
    def setUp(self):
        self.dir = tempfile.TemporaryDirectory()
        self.addCleanup(self.dir.cleanup)

    def path(self, name, content):
        p = os.path.join(self.dir.name, name)
        with open(p, 'wb') as f:
            f.write(content)
        return p

    def test_uninitialized_is_safe(self):  # 91
        v = pylib.VPKFile.__new__(pylib.VPKFile)
        self.assertFalse(v.check('x'))

    def test_corrupt_files_raise(self):  # 92, 93
        bad = [b'x' * 100 + struct.pack('<IIB', 3, 1000, 0),
               struct.pack('<I', 0x7fffffff) + b'\0' * 20 + struct.pack('<IIB', 1, 0, 0),
               rand(4096)]
        for i, content in enumerate(bad):
            with self.assertRaises(ValueError):
                pylib.VPKFile(self.path(f'bad{i}.vpk', content))

    def test_vtmb_long_names_and_paths(self):  # 90
        name = b'sound/' + b'x' * 40 + b'.wav'
        payload = b'hello vpk'
        blob = (payload + struct.pack('<I', len(name)) + name + struct.pack('<II', 0, len(payload))
                + struct.pack('<IIB', 1, len(payload), 0))
        v = pylib.VPKFile(self.path('pack' + 'y' * 60 + '.vpk', blob))
        self.assertEqual(v.find_file(name.decode()), payload)

    def test_v2_lookup_case_and_truncation(self):  # 94, 95, 96
        long_name = b'n' * 1500
        blob = vpk_v2([(b'vtf', b'materials', b'foo', 0, 5), (b'txt', b'x', long_name, 5, 3)], b'helloabc')
        v = pylib.VPKFile(self.path('ok_dir.vpk', blob))
        self.assertTrue(v.check('Materials/Foo.vtf'))
        self.assertEqual(v.find_file('Materials/Foo.vtf'), b'hello')
        self.assertEqual(v.find_file('x/' + long_name.decode() + '.txt'), b'abc')
        self.assertEqual(list(v.glob('MATERIALS/*')), [('materials/foo.vtf', b'hello')])

        t = pylib.VPKFile(self.path('tr_dir.vpk', blob[:-2]))
        with self.assertRaises((ValueError, OSError)):
            t.find_file('x/' + long_name.decode() + '.txt')
        self.assertEqual(t.find_file('materials/foo.vtf'), b'hello')
        with self.assertRaises(ValueError):
            pylib.VPKFile(self.path('cut_dir.vpk', blob[:40]))

    def test_glob_iterator_not_instantiable(self):  # 102
        v = pylib.VPKFile(self.path('a_dir.vpk', vpk_v2([(b'vtf', b'm', b'f', 0, 1)], b'z')))
        with self.assertRaises(TypeError):
            type(v.glob('*'))()

    def test_glob_follows_fnmatch_rules(self):  # 105
        import fnmatch
        names = [(b'vtf', b'materials/a', b'foo', 0, 1), (b'vmt', b'materials/b', b'fao', 1, 1),
                 (b'mdl', b'models', b'f[x', 2, 1)]
        v = pylib.VPKFile(self.path('g_dir.vpk', vpk_v2(names, b'xyz')))
        all_names = [n for n, _ in v.glob('*')]
        for pattern in ('*', 'materials/*', '*/f?o.*', '*[!t]', 'materials/[a-b]/*', 'models/f[x.mdl',
                        '*.v[mt][tf]', 'MODELS/*'):
            expected = sorted(n for n in all_names if fnmatch.fnmatchcase(n, pattern.lower()))
            self.assertEqual(sorted(n for n, _ in v.glob(pattern)), expected, pattern)
        self.assertEqual(len(all_names), 3)


if __name__ == '__main__':
    unittest.main()
