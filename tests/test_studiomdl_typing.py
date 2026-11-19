"""pylib.studiomdl exports a type annotation for every public function, method and attribute (__typing__), which
generate_pyi.py turns into a typed stub."""
import ast
import enum
import inspect
import tempfile
import unittest
from pathlib import Path

try:
    import pylib.studiomdl as smdl
except ImportError:  # arm64 macOS build
    smdl = None


def _members():
    """(qualified name, object, is_attribute) of every public function, method and getset."""
    for name in dir(smdl):
        if name.startswith("_"):
            continue
        obj = getattr(smdl, name)
        if inspect.isbuiltin(obj):
            yield name, obj, False
        elif isinstance(obj, type) and obj.__module__ == "pylib.studiomdl" and not issubclass(obj, enum.Enum) \
                and not issubclass(obj, BaseException) and obj is not smdl.Message:
            for member_name, member in vars(obj).items():
                if member_name.startswith("_"):
                    continue
                if inspect.isgetsetdescriptor(member):
                    yield f"{name}.{member_name}", member, True
                elif inspect.ismethoddescriptor(member):
                    yield f"{name}.{member_name}", member, False


@unittest.skipIf(smdl is None, "pylib.studiomdl is not built for this platform")
class TypingTests(unittest.TestCase):
    def test_every_public_member_is_annotated(self):
        missing = [name for name, _, _ in _members() if name not in smdl.__typing__]
        self.assertEqual(missing, [])

    def test_annotated_signatures_match_the_runtime_parameters(self):
        for name, obj, is_attribute in _members():
            if is_attribute:
                continue
            with self.subTest(name):
                typed = smdl.__typing__[name]
                tree = ast.parse(f"def f{typed}: ...").body[0]
                params = [a.arg for a in tree.args.posonlyargs + tree.args.args + tree.args.kwonlyargs]
                runtime = [p for p in inspect.signature(obj).parameters if p != "self"]
                self.assertEqual(params, runtime)
                self.assertIsNotNone(tree.returns, "missing return annotation")
                self.assertTrue(all(a.annotation is not None
                                    for a in tree.args.posonlyargs + tree.args.args + tree.args.kwonlyargs),
                                "missing parameter annotation")

    def test_attribute_annotations_are_expressions(self):
        for name, _, is_attribute in _members():
            if is_attribute:
                with self.subTest(name):
                    ast.parse(smdl.__typing__[name], mode="eval")

    def test_generated_stub_is_valid_and_typed(self):
        import generate_pyi  # repository root
        with tempfile.TemporaryDirectory() as directory:
            generate_pyi.generate_pyi("pylib", "", directory, True)
            stub = Path(directory, "pylib", "studiomdl.pyi").read_text(encoding="utf-8")
        ast.parse(stub)
        self.assertIn("def add_bone(self, name: str", stub)
        self.assertIn("StrPath = str | os.PathLike[str]", stub)


if __name__ == "__main__":
    unittest.main()
