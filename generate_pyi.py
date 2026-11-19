import inspect
import importlib
import sys
import textwrap
from dataclasses import dataclass, field
from enum import Enum, IntEnum, IntFlag
from pathlib import Path
from types import ModuleType
from typing import Any, TextIO, get_origin, get_args

_IGNORE = {
    "__doc__", "__file__", "__name__", "__package__", "__loader__", "__module__", "__path__", "__spec__", "__all__",
    "__cached__", "__builtins__"
}
_DISALLOW_INSTANTIATION = 1 << 7  # Py_TPFLAGS_DISALLOW_INSTANTIATION: library-created objects, no __init__
_BASETYPE = 1 << 10  # Py_TPFLAGS_BASETYPE: the class can be subclassed


@dataclass
class IRImport:
    """Represents 'from module import name'."""
    module: str
    names: set[str] = field(default_factory=set)


@dataclass
class IRFunction:
    """Represents a function or method signature."""
    name: str
    signature: str
    decorator: str | None
    doc: str | None


@dataclass
class IRProperty:
    """Represents a property with a return type."""
    name: str
    return_type: str


@dataclass
class IRVar:
    """Represents a variable or annotated attribute."""
    name: str
    type_repr: str


@dataclass
class IREnum:
    """Represents an Enum or IntEnum class."""
    name: str
    base: str
    members: list[tuple[str, Any]]  # (name, value)
    doc: str | None


@dataclass
class IRClass:
    """Represents a non-enum class."""
    name: str
    bases: list[str]
    doc: str | None
    final: bool = False  # can't be subclassed at runtime
    attrs: list[IRVar] = field(default_factory=list)
    props: list[IRProperty] = field(default_factory=list)
    methods: list[IRFunction] = field(default_factory=list)
    classmethods: list[IRFunction] = field(default_factory=list)
    staticmethods: list[IRFunction] = field(default_factory=list)


@dataclass
class IRModule:
    """Represents a module and its contents."""
    fullname: str
    doc: str | None
    imports: dict[str, IRImport] = field(default_factory=dict)
    classes: list[IRClass | IREnum] = field(default_factory=list)
    functions: list[IRFunction] = field(default_factory=list)
    vars: list[IRVar] = field(default_factory=list)
    submodules: dict[str, "IRModule"] = field(default_factory=dict)
    header: str | None = None

    def add_import(self, module: str, name: str) -> None:
        """Add a single imported name under a module."""
        imp = self.imports.get(module)
        if imp is None:
            imp = IRImport(module, set())
            self.imports[module] = imp
        imp.names.add(name)


def _indent(level: int) -> str:
    """Return indentation string."""
    return "    " * level


def _typename(tp: Any) -> str:
    """Render a type into a string suitable for stubs."""
    if tp is inspect.Signature.empty:
        return "Any"
    try:
        if isinstance(tp, type):
            return tp.__name__ if tp.__module__ in ("builtins", "typing") else f"{tp.__module__}.{tp.__name__}"
        origin = get_origin(tp)
        if origin is None:
            return getattr(tp, "__name__", repr(tp))
        args = ", ".join(_typename(a) for a in get_args(tp))
        oname = getattr(origin, "__name__", str(origin).replace("typing.", ""))
        return f"{oname}[{args}]" if args else oname
    except Exception:
        return "Any"


@dataclass
class Typing:
    """Type annotations a native module exports for its members.

    ``__typing__``: {"function" or "Class.member": annotation}; functions/methods map to a signature without self
    ("(a: int, b: str = 'x') -> Bone"), attributes to a type ("float | None"). ``__typing_readonly__``: attribute
    names without a setter. ``__pyi_header__``: imports and aliases the annotations use, emitted verbatim.
    """
    entries: dict[str, str] = field(default_factory=dict)
    readonly: set[str] = field(default_factory=set)
    header: str | None = None

    @classmethod
    def of(cls, module: ModuleType) -> "Typing":
        return cls(dict(getattr(module, "__typing__", None) or {}),
                   set(getattr(module, "__typing_readonly__", None) or ()),
                   getattr(module, "__pyi_header__", None))

    def method_signature(self, key: str, is_method: bool) -> str | None:
        typed = self.entries.get(key)
        if typed is None or not typed.startswith("("):
            return None
        if not is_method:
            return typed
        return "(self" + (", " if not typed.startswith("()") else "") + typed[1:]


def _is_data_descriptor(raw: Any) -> bool:
    return (inspect.isgetsetdescriptor(raw) or inspect.ismemberdescriptor(raw)
            or type(raw).__name__ == "_tuplegetter")


def _safe_signature(obj: Any) -> inspect.Signature | None:
    """Return a safe signature or None if not introspectable."""
    try:
        return inspect.signature(obj)
    except Exception:
        return None


def _iter_public_names(obj: Any, include_private: bool) -> list[str]:
    """Return names ordered as: members, properties, methods, classmethods, staticmethods (for classes); else alphabetical."""
    names = [n for n in dir(obj) if n not in _IGNORE]
    if not include_private:
        names = [n for n in names if not n.startswith("_")]
    if not isinstance(obj, type):
        return sorted(names)
    if isinstance(obj, type):
        names.append("__init__")
    cls = obj
    ranked: list[tuple[int, str]] = []
    for name in names:
        try:
            val = getattr(cls, name)
        except Exception:
            continue
        raw = cls.__dict__.get(name, val)
        if isinstance(raw, staticmethod):
            cat = 4
        elif isinstance(raw, classmethod):
            cat = 3
        elif isinstance(val, property):
            cat = 1
        elif callable(val):
            cat = 2
        else:
            cat = 0
        ranked.append((cat, name))
    ranked.sort(key=lambda x: (x[0], x[1]))
    return [n for _, n in ranked]


def _collect_submodules_any(module: ModuleType) -> dict[str, ModuleType]:
    """Collect submodules via attributes and sys.modules, independent of __path__."""
    prefix = module.__name__ + "."
    found: dict[str, ModuleType] = {}
    for attr in dir(module):
        try:
            obj = getattr(module, attr)
        except Exception:
            continue
        if isinstance(obj, ModuleType):
            synthetic = f"{module.__name__}.{attr}"
            found.setdefault(synthetic, obj)
    for modname, modobj in list(sys.modules.items()):
        if isinstance(modobj, ModuleType) and modname.startswith(prefix):
            found.setdefault(modname, modobj)
    return found


def _sig_to_text(sig: inspect.Signature | None, is_method: bool) -> str:
    """Convert a signature to a stub text."""
    if sig is None:
        return "(*args: Any, **kwargs: Any) -> Any"
    params = []
    for pname, p in sig.parameters.items():
        ann = _typename(p.annotation)
        if p.kind is p.VAR_POSITIONAL:
            params.append(f"*{pname}: {ann}")
        elif p.kind is p.VAR_KEYWORD:
            params.append(f"**{pname}: {ann}")
        else:
            default = "" if p.default is inspect._empty else " = ..."
            params.append(f"{pname}: {ann}{default}")
    ret = _typename(sig.return_annotation)
    return f"({', '.join(params)}) -> {ret}"


def _collect_function_ir(name: str, obj: Any, is_method: bool, typed: str | None = None) -> IRFunction:
    """Collect IR for a function or method; typed: an exported signature replacing the introspected one."""
    signature = typed if typed is not None else _sig_to_text(_safe_signature(obj), is_method)
    if name == "__init__" and typed is None:
        signature = signature.rsplit(" -> ", 1)[0] + " -> None"
    return IRFunction(name=name, signature=signature, decorator=None, doc=inspect.getdoc(obj))


def _collect_property_ir(name: str, prop: property) -> IRProperty:
    """Collect IR for a property."""
    rtype = "Any"
    if prop.fget:
        sig = _safe_signature(prop.fget)
        if sig is not None:
            rtype = _typename(sig.return_annotation)
    return IRProperty(name=name, return_type=rtype)


def _collect_class_ir(cls: type, include_private: bool, im: IRModule, typing: Typing | None = None) -> IRClass | IREnum:
    """Collect IR for a class, mapping Enums to IREnum and collecting imports as needed."""
    if issubclass(cls, IntFlag):
        im.add_import("enum", "IntFlag")
        members = [(name, member.value) for name, member in getattr(cls, "__members__", {}).items()]
        return IREnum(name=cls.__name__, base="IntFlag", members=members, doc=inspect.getdoc(cls))
    elif issubclass(cls, IntEnum):
        base = "IntEnum" if issubclass(cls, int) else "Enum"
        im.add_import("enum", base)
        members = [(name, member.value) for name, member in getattr(cls, "__members__", {}).items()]
        return IREnum(name=cls.__name__, base=base, members=members, doc=inspect.getdoc(cls))


    bases = [b.__name__ for b in cls.__bases__ if b is not object]
    if issubclass(cls, tuple) and hasattr(cls, "_fields"):  # collections.namedtuple
        im.add_import("typing", "NamedTuple")
        nt = IRClass(name=cls.__name__, bases=["NamedTuple"], doc=inspect.getdoc(cls))
        for field_name in cls._fields:
            annotation = (typing.entries if typing else {}).get(f"{cls.__name__}.{field_name}", "Any")
            nt.attrs.append(IRVar(name=field_name, type_repr=annotation))
        return nt
    ic = IRClass(name=cls.__name__, bases=bases, doc=inspect.getdoc(cls),
                 final=not cls.__flags__ & _BASETYPE and cls.__module__ != "builtins")
    if ic.final:
        im.add_import("typing", "final")
    ann2 = inspect.get_annotations(cls, eval_str=True)
    ann = ann2 or getattr(cls, "__annotations__", {}) or {}
    for aname, atype in ann.items():
        if not include_private and isinstance(aname, str) and aname.startswith("_"):
            continue
        ic.attrs.append(IRVar(name=aname, type_repr=_typename(atype)))
    typing = typing or Typing()
    prefix = f"{cls.__name__}."
    seen: set[str] = set(ann)
    for name in _iter_public_names(cls, include_private):
        key = prefix + name
        owner = next((base for base in cls.__mro__ if name in base.__dict__), None)
        if key not in typing.entries and (owner is None or owner.__module__ == "builtins"):
            continue  # inherited from object / BaseException / tuple: documented by typeshed
        if name == "__init__" and cls.__flags__ & _DISALLOW_INSTANTIATION:
            continue
        member = getattr(cls, name)
        raw = cls.__dict__.get(name, member)
        seen.add(name)
        if isinstance(member, property):
            ic.props.append(_collect_property_ir(name, member))
        elif isinstance(raw, staticmethod):
            fn = _collect_function_ir(name, raw.__func__, is_method=False)
            fn.decorator = "staticmethod"
            ic.staticmethods.append(fn)
        elif isinstance(raw, classmethod):
            fn = _collect_function_ir(name, raw.__func__, is_method=False)
            fn.decorator = "classmethod"
            ic.classmethods.append(fn)
        elif callable(member) and not _is_data_descriptor(member):
            ic.methods.append(_collect_function_ir(name, member, is_method=True,
                                                   typed=typing.method_signature(key, True)))
        elif key in typing.entries and key in typing.readonly:
            ic.props.append(IRProperty(name=name, return_type=typing.entries[key]))
        elif name not in ann:
            ic.attrs.append(IRVar(name=name, type_repr=typing.entries.get(key, "Any")))
    # Exported annotations for members introspection can't see (instance attributes of exceptions, ...)
    for key, annotation in typing.entries.items():
        name = key[len(prefix):]
        if key.startswith(prefix) and "." not in name and name not in seen and not annotation.startswith("("):
            ic.attrs.append(IRVar(name=name, type_repr=annotation))
    # A constructor implemented in tp_new: "Cls.__new__" = "(params) -> Cls"
    constructor = typing.entries.get(prefix + "__new__")
    if constructor:
        signature = "(cls" + (", " if not constructor.startswith("()") else "") + constructor[1:]
        ic.methods.insert(0, IRFunction(name="__new__", signature=signature, decorator=None, doc=None))
    return ic


def _collect_module_ir(module: ModuleType, include_private: bool, visited: set[str]) -> IRModule:
    """Collect the IR for a module and its discovered submodules."""
    if module.__name__ in visited:
        return IRModule(module.__name__, None)
    visited.add(module.__name__)
    im = IRModule(fullname=module.__name__, doc=inspect.getdoc(module))
    im.add_import("typing", "Any")
    typing = Typing.of(module)
    im.header = typing.header
    names = _iter_public_names(module, include_private)
    for name in names:
        try:
            obj = getattr(module, name)
        except Exception:
            continue
        if isinstance(obj, ModuleType):
            continue
        if isinstance(obj, type):
            im.classes.append(_collect_class_ir(obj, include_private, im, typing))
        elif callable(obj):
            im.functions.append(_collect_function_ir(name, obj, is_method=False,
                                                     typed=typing.method_signature(name, False)))
    ann = getattr(module, "__annotations__", {}) or {}
    for n, tp in ann.items():
        if not include_private and isinstance(n, str) and n.startswith("_"):
            continue
        im.vars.append(IRVar(name=n, type_repr=_typename(tp)))
    for name in names:
        if name in ann:
            continue
        try:
            obj = getattr(module, name)
        except Exception:
            continue
        if not callable(obj) and not isinstance(obj, (type, ModuleType, property, staticmethod, classmethod)):
            im.vars.append(IRVar(name=name, type_repr=typing.entries.get(name, "Any")))
    subs = _collect_submodules_any(module)
    for fullname, submod in sorted(subs.items()):
        leaf = fullname.rsplit(".", 1)[-1]
        im.submodules[leaf] = _collect_module_ir(submod, include_private, visited)
    return im


def _write_docstring(f: TextIO, doc: str | None, indent: int) -> None:
    """Write a trimmed docstring block if present."""
    if not doc:
        return
    doc = inspect.cleandoc(doc)
    if not doc:
        return
    lines = textwrap.dedent(doc).splitlines()
    f.write(f"{_indent(indent)}\"\"\"\n")
    for line in lines:
        f.write(f"{_indent(indent)}{line}\n")
    f.write(f"{_indent(indent)}\"\"\"\n")


def _emit_function_stub(fn: IRFunction, f: TextIO, indent: int) -> None:
    """Emit a function stub from IR."""
    if fn.decorator:
        f.write(f"{_indent(indent)}@{fn.decorator}\n")
    f.write(f"{_indent(indent)}def {fn.name}{fn.signature}:\n")
    _write_docstring(f, fn.doc, indent + 1)
    f.write(f"{_indent(indent + 1)}...\n\n")


def _emit_class_stub(ic: IRClass | IREnum, f: TextIO, indent: int) -> None:
    """Emit a class or enum stub from IR."""
    if isinstance(ic, IREnum):
        f.write(f"{_indent(indent)}class {ic.name}({ic.base}):\n")
        _write_docstring(f, ic.doc, indent + 1)
        # `NAME = value` declares a member; `NAME: Type` would declare a non-member attribute
        for name, value in ic.members:
            literal = repr(value) if isinstance(value, (int, str, float, bool)) else "..."
            f.write(f"{_indent(indent + 1)}{name} = {literal}\n")
        f.write("\n")
        return
    base_txt = f"({', '.join(ic.bases)})" if ic.bases else ""
    if ic.final:
        f.write(f"{_indent(indent)}@final\n")
    f.write(f"{_indent(indent)}class {ic.name}{base_txt}:\n")
    _write_docstring(f, ic.doc, indent + 1)
    for v in ic.attrs:
        f.write(f"{_indent(indent + 1)}{v.name}: {v.type_repr}\n")
    if ic.attrs:
        f.write("\n")
    for p in ic.props:
        f.write(f"{_indent(indent + 1)}@property\n")
        f.write(f"{_indent(indent + 1)}def {p.name}(self) -> {p.return_type}: ...\n\n")
    for m in ic.methods:
        _emit_function_stub(m, f, indent + 1)
    for m in ic.classmethods:
        _emit_function_stub(m, f, indent + 1)
    for m in ic.staticmethods:
        _emit_function_stub(m, f, indent + 1)


def _emit_imports(im: IRModule, f: TextIO) -> None:
    """Emit collected imports for a module."""
    if im.imports:
        for mod in sorted(im.imports.keys()):
            names = sorted(im.imports[mod].names)
            f.write(f"from {mod} import {', '.join(names)}\n")
        f.write("\n")


def _serialize_module_to_file(im: IRModule, target: Path, as_package: bool) -> None:
    """Serialize a single IRModule to a .pyi file."""
    target.parent.mkdir(parents=True, exist_ok=True)
    with open(target, "w", encoding="utf-8") as f:
        f.write("from __future__ import annotations\n\n")
        _emit_imports(im, f)
        if im.header:
            f.write(im.header.rstrip("\n") + "\n\n")
        _write_docstring(f, im.doc, 0)
        for c in im.classes:
            _emit_class_stub(c, f, 0)
        for fn in im.functions:
            _emit_function_stub(fn, f, 0)
        for v in im.vars:
            f.write(f"{v.name}: {v.type_repr}\n")


def _serialize_tree(im: IRModule, root_dir: Path, as_package_root: bool) -> None:
    """Serialize IR tree into files."""
    if as_package_root:
        pkg_dir = root_dir / im.fullname
        pkg_dir.mkdir(parents=True, exist_ok=True)
        _serialize_module_to_file(im, pkg_dir / "__init__.pyi", as_package=True)
        for leaf, sub in im.submodules.items():
            sub_is_pkg = bool(sub.submodules)
            if sub_is_pkg:
                sub_dir = pkg_dir / leaf
                _serialize_module_to_file(sub, sub_dir / "__init__.pyi", as_package=True)
                _serialize_tree(sub, pkg_dir, False)
            else:
                _serialize_module_to_file(sub, pkg_dir / f"{leaf}.pyi", as_package=False)
    else:
        here = root_dir / im.fullname
        _serialize_module_to_file(im, here.with_suffix(".pyi"), as_package=False)
        for leaf, sub in im.submodules.items():
            sub_is_pkg = bool(sub.submodules)
            if sub_is_pkg:
                sub_dir = here.parent / leaf
                _serialize_module_to_file(sub, sub_dir / "__init__.pyi", as_package=True)
                _serialize_tree(sub, here.parent, False)
            else:
                _serialize_module_to_file(sub, here.parent / f"{leaf}.pyi", as_package=False)


def generate_pyi(module_name: str, module_path: str, output_path: str, is_multi_module: bool,
                 include_private: bool = False) -> None:
    """Build IR, collect imports, and serialize to .pyi files."""
    out = Path(output_path)
    out.mkdir(parents=True, exist_ok=True)
    if module_path:
        sys.path.insert(0, module_path)
    module = importlib.import_module(module_name)
    visited: set[str] = set()
    ir = _collect_module_ir(module, include_private, visited)
    _serialize_tree(ir, out, as_package_root=is_multi_module)


if __name__ == "__main__":
    import argparse

    parser = argparse.ArgumentParser(description="Generate .pyi stubs for the pylib native module.")
    parser.add_argument("--module-dir", default="./cmake-build-release", help="directory holding the built module")
    parser.add_argument("--output", default="./generated_pyi", help="stub output directory")
    args = parser.parse_args()
    generate_pyi("pylib", args.module_dir, args.output, True, include_private=False)
