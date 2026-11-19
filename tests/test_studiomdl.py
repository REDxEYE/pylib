"""pylib.studiomdl core: Model settings, bones, errors and compile_qc.

pylib.studiomdl only exists in x86_64 builds (studiomdl_v2 uses x86 intrinsics); the tests skip elsewhere.
"""
import os
import tempfile
import threading
import unittest

try:
    import pylib.studiomdl as smdl
except ImportError:  # arm64 macOS build
    smdl = None

TRIANGLE_SMD = """version 1
nodes
0 "root" -1
end
skeleton
time 0
0 0 0 0 0 0 0
end
triangles
mat
0 0 0 0 0 0 1 0 0
0 10 0 0 0 0 1 1 0
0 0 10 0 0 0 1 0 1
end
"""

TRIANGLE_QC = """$modelname "test/tri.mdl"
$cdmaterials "models/test"
$body body "tri.smd"
$sequence idle "tri.smd"
"""


@unittest.skipIf(smdl is None, "pylib.studiomdl is not built for this platform")
class ModelSettingsTests(unittest.TestCase):
    def test_settings_read_back_the_assigned_value_and_default_to_none(self):
        model = smdl.Model("props/crate")
        self.assertIsNone(model.scale)
        model.scale = 2
        model.up_axis = smdl.UpAxis.Y
        model.contents = smdl.Contents.SOLID | smdl.Contents.GRATE
        model.bbox = ([0, 0, 0], (1, 2, 3))
        self.assertEqual(model.scale, 2.0)
        self.assertIs(model.up_axis, smdl.UpAxis.Y)
        self.assertEqual(model.contents, smdl.Contents.SOLID | smdl.Contents.GRATE)
        self.assertEqual(model.bbox, ((0.0, 0.0, 0.0), (1.0, 2.0, 3.0)))
        # Settings are per model
        self.assertIsNone(smdl.Model("other").scale)

    def test_invalid_values_are_rejected_before_reaching_studiomdl(self):
        model = smdl.Model("a")
        with self.assertRaises(TypeError):
            model.scale = "large"
        with self.assertRaises(ValueError):
            model.origin = (1, 2)
        with self.assertRaises(TypeError):
            model.scale = None
        with self.assertRaises(AttributeError):
            del model.scale
        self.assertIsNone(model.scale)

    def test_name_is_the_studiomdl_name(self):
        model = smdl.Model("props/crate")
        model.name = "props/barrel"
        self.assertEqual(model.name, "props/barrel")
        self.assertEqual(repr(model), "<Model 'props/barrel'>")


@unittest.skipIf(smdl is None, "pylib.studiomdl is not built for this platform")
class BoneTests(unittest.TestCase):
    def test_bones_are_views_comparing_by_model_and_id(self):
        model = smdl.Model("a")
        root = model.add_bone("root")
        child = model.add_bone("child", "root", (0, 0, 10), euler=(0, 0, 1.5))
        self.assertEqual(model.bones, (root, child))
        self.assertEqual(child.parent, root)
        self.assertIsNone(root.parent)
        self.assertEqual((child.name, child.index), ("child", 1))
        self.assertEqual(model.find_bone("child"), child)
        self.assertEqual(len({root, model.bones[0], child}), 2)
        self.assertNotEqual(smdl.Model("a").add_bone("root"), root)
        self.assertIs(child.model, model)

    def test_bone_settings_are_per_bone(self):
        model = smdl.Model("a")
        root = model.add_bone("root")
        child = model.add_bone("child", root)
        child.surface_prop = "metal"
        child.save_frame = smdl.BoneSaveFrame.POSITION | smdl.BoneSaveFrame.ROTATION
        self.assertEqual(child.surface_prop, "metal")
        self.assertIsNone(root.surface_prop)
        self.assertEqual(child.save_frame, smdl.BoneSaveFrame.POSITION | smdl.BoneSaveFrame.ROTATION)
        model.root_bone = "child"
        self.assertEqual(model.root_bone, child)
        model.root_bone = None
        self.assertIsNone(model.root_bone)

    def test_bone_arguments_must_belong_to_the_model(self):
        model = smdl.Model("a")
        foreign = smdl.Model("b").add_bone("root")
        with self.assertRaisesRegex(ValueError, "another model"):
            model.add_bone("child", foreign)
        with self.assertRaises(smdl.NotFoundError) as caught:
            model.add_bone("child", "missing")
        self.assertIsInstance(caught.exception, KeyError)
        self.assertEqual(caught.exception.status, 4)

    def test_rotation_is_a_quaternion_or_euler_angles_not_both(self):
        model = smdl.Model("a")
        with self.assertRaisesRegex(TypeError, "not both"):
            model.add_bone("root", rotation=(0, 0, 0, 1), euler=(0, 0, 0))

    def test_duplicate_names_raise_duplicate_name_error(self):
        model = smdl.Model("a")
        model.add_bone("root")
        with self.assertRaises(smdl.DuplicateNameError) as caught:
            model.add_bone("root")
        self.assertIsInstance(caught.exception, ValueError)

    def test_views_are_not_constructible(self):
        with self.assertRaises(TypeError):
            smdl.Bone()


@unittest.skipIf(smdl is None, "pylib.studiomdl is not built for this platform")
class CompileTests(unittest.TestCase):
    def test_failed_compile_raises_compile_error_with_the_messages(self):
        model = smdl.Model("empty")
        with self.assertRaises(smdl.CompileError) as caught:
            model.compile()
        error = caught.exception
        self.assertIn("no sequences", str(error))
        self.assertTrue(error.messages)
        self.assertIs(error.messages[-1].severity, smdl.Severity.ERROR)
        # The model is usable again after a failed compile
        model.scale = 1.0

    def test_compile_qc_reads_files_from_memory(self):
        result = smdl.compile_qc("/virtual/tri.qc",
                                 files={"/virtual/tri.qc": TRIANGLE_QC, "/virtual/tri.smd": TRIANGLE_SMD.encode()})
        files = result.files
        self.assertEqual(sorted(files), ["models/test/tri.dx90.vtx", "models/test/tri.mdl", "models/test/tri.vvd"])
        self.assertEqual(files["models/test/tri.mdl"][:4], b"IDST")
        self.assertEqual(result.warning_count, 0)
        self.assertTrue(all(isinstance(m, smdl.Message) for m in result.messages))

    def test_result_save_writes_the_files_under_the_directory(self):
        result = smdl.compile_qc("/virtual/tri.qc", files={"/virtual/tri.qc": TRIANGLE_QC,
                                                           "/virtual/tri.smd": TRIANGLE_SMD})
        with tempfile.TemporaryDirectory() as directory:
            result.save(directory)
            path = os.path.join(directory, "models", "test", "tri.mdl")
            with open(path, "rb") as file:
                self.assertEqual(file.read(), result.files["models/test/tri.mdl"])

    def test_failed_qc_compile_raises_qc_error(self):
        with self.assertRaises(smdl.QcError) as caught:
            smdl.compile_qc("/virtual/bad.qc",
                            files={"/virtual/bad.qc": '$modelname "x.mdl"\n$body body "missing.smd"\n'})
        error = caught.exception
        self.assertIsInstance(error, smdl.CompileError)
        self.assertIn("missing.smd", str(error))
        self.assertIsInstance(error.line, int)

    def test_compile_qc_files_must_be_bytes_or_str(self):
        with self.assertRaises(TypeError):
            smdl.compile_qc("/virtual/tri.qc", files={"/virtual/tri.qc": 1})

    def test_compiles_run_without_the_gil(self):
        # Two threads compiling concurrently finish with identical output (studiomdl serializes internally).
        files = {"/virtual/tri.qc": TRIANGLE_QC, "/virtual/tri.smd": TRIANGLE_SMD}
        results = []
        threads = [threading.Thread(target=lambda: results.append(smdl.compile_qc("/virtual/tri.qc", files=files)))
                   for _ in range(2)]
        for thread in threads:
            thread.start()
        for thread in threads:
            thread.join()
        self.assertEqual(len(results), 2)
        self.assertEqual(results[0].files, results[1].files)


if __name__ == "__main__":
    unittest.main()
