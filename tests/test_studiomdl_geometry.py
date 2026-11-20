"""pylib.studiomdl geometry and flex: meshes, shapes, flexes, body groups and LODs.

pylib.studiomdl only exists in x86_64 builds (studiomdl_v2 uses x86 intrinsics); the tests skip elsewhere.
"""
import unittest

try:
    import pylib.studiomdl as smdl
except ImportError:  # arm64 macOS build
    smdl = None

try:
    import numpy
except ImportError:
    numpy = None

SIZE = 10.0
# (normal, u axis, v axis) of the six faces of a box
FACES = (
    ((1, 0, 0), (0, 1, 0), (0, 0, 1)),
    ((-1, 0, 0), (0, 0, 1), (0, 1, 0)),
    ((0, 1, 0), (0, 0, 1), (1, 0, 0)),
    ((0, -1, 0), (1, 0, 0), (0, 0, 1)),
    ((0, 0, 1), (1, 0, 0), (0, 1, 0)),
    ((0, 0, -1), (0, 1, 0), (1, 0, 0)),
)


def cross(a, b):
    return (a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0])


def box_data(size=SIZE):
    """Positions, normals, uvs and triangles of a box with 4 vertices per face (counter-clockwise from outside)."""
    positions, normals, uvs, triangles = [], [], [], []
    for normal, u, v in FACES:
        base = len(positions)
        for a, b in ((-1, -1), (1, -1), (1, 1), (-1, 1)):
            positions.append(tuple((normal[i] + a * u[i] + b * v[i]) * size / 2 for i in range(3)))
            normals.append(normal)
            uvs.append(((a + 1) / 2, (b + 1) / 2))
        c = cross(u, v)
        if sum(c[i] * normal[i] for i in range(3)) > 0:
            triangles += [(base, base + 1, base + 2), (base, base + 2, base + 3)]
        else:
            triangles += [(base, base + 2, base + 1), (base, base + 3, base + 2)]
    return positions, normals, uvs, triangles


def fill_box(geometry, size=SIZE, material="boxmat"):
    positions, normals, uvs, triangles = box_data(size)
    geometry.set_positions(positions)
    geometry.set_normals(normals)
    geometry.set_uvs(uvs)
    geometry.add_material(material)
    geometry.set_triangles(triangles)


def new_model(name="test/box"):
    """A model with a root bone and a one frame idle sequence (studiomdl needs a sequence)."""
    model = smdl.Model(name)
    model.add_material_path("models/test/")
    root = model.add_bone("root")
    animation = model.create_animation("idle_anim", 30.0, 1)
    model.create_sequence("idle").add_animation(animation)
    return model, root


def box_model(name="test/box", size=SIZE):
    model, root = new_model(name)
    mesh = model.create_mesh("box")
    fill_box(mesh.geometry, size)
    return model, mesh, root


def warnings_and_errors(result):
    return [m.text for m in result.messages if m.severity != smdl.Severity.MESSAGE]


@unittest.skipIf(smdl is None, "pylib.studiomdl is not built for this platform")
class GeometryTests(unittest.TestCase):
    def test_box_compiles_to_mdl_vvd_and_vtx(self):
        model, mesh, root = box_model()
        geometry = mesh.geometry
        positions, normals, uvs, triangles = box_data()
        material = 0
        geometry.set_weights([[root.index]] * 24, [[1.0]] * 24)
        geometry.set_triangles(triangles, [material] * len(triangles))
        self.assertEqual((geometry.vertex_count, geometry.triangle_count, geometry.material_count), (24, 12, 1))
        result = model.compile()
        self.assertEqual(sorted(result.files),
                         ["models/test/box.dx90.vtx", "models/test/box.mdl", "models/test/box.vvd"])
        self.assertEqual(result.files["models/test/box.mdl"][:4], b"IDST")
        self.assertEqual(result.files["models/test/box.vvd"][:4], b"IDSV")
        self.assertEqual(warnings_and_errors(result), [])

    def test_geometry_size_changes_the_output(self):
        small = box_model("test/box", 10.0)[0].compile().files
        large = box_model("test/box", 20.0)[0].compile().files
        self.assertNotEqual(small["models/test/box.vvd"], large["models/test/box.vvd"])

    @unittest.skipIf(numpy is None, "numpy is not installed")
    def test_numpy_arrays_give_the_same_model_as_lists(self):
        positions, normals, uvs, triangles = box_data()
        model, root = new_model()
        geometry = model.create_mesh("box").geometry
        geometry.set_positions(numpy.array(positions, dtype=numpy.float32))
        geometry.set_normals(numpy.array(normals, dtype=numpy.float32))
        geometry.set_uvs(numpy.array(uvs, dtype=numpy.float32))
        geometry.add_material("boxmat")
        geometry.set_weights(numpy.zeros((24, 1), dtype=numpy.uint32), numpy.ones((24, 1), dtype=numpy.float32))
        geometry.set_triangles(numpy.array(triangles, dtype=numpy.uint32), numpy.zeros(12, dtype=numpy.uint32))
        expected = box_model()[0].compile().files
        self.assertEqual(model.compile().files, expected)

    def test_quads_compile_in_a_subd_model(self):
        positions, normals, uvs, triangles = box_data()
        model, root = new_model("test/subd")
        model.subd = True
        geometry = model.create_mesh("box").geometry
        geometry.set_positions(positions)
        geometry.set_normals(normals)
        geometry.set_uvs(uvs)
        geometry.add_material("boxmat")
        geometry.set_quads([(4 * i, 4 * i + 1, 4 * i + 2, 4 * i + 3) for i in range(6)], [0] * 6)
        self.assertEqual((geometry.quad_count, geometry.triangle_count), (6, 0))
        self.assertEqual(model.compile().files["models/test/subd.mdl"][:4], b"IDST")

    def test_corner_normals_and_uvs_replace_the_indexed_ones(self):
        positions, normals, uvs, triangles = box_data()
        model, root = new_model()
        geometry = model.create_mesh("box").geometry
        geometry.set_positions(positions)
        geometry.add_material("boxmat")
        geometry.set_triangles(triangles)
        corners = [corner for triangle in triangles for corner in triangle]
        geometry.set_corner_normals([normals[i] for i in corners])
        geometry.set_corner_uvs([uvs[i] for i in corners])
        self.assertEqual(model.compile().files["models/test/box.mdl"][:4], b"IDST")

    def test_flip_triangles_and_scale_are_properties(self):
        model, mesh, root = box_model()
        geometry = mesh.geometry
        self.assertIsNone(geometry.flip_triangles)
        before = model.compile().files
        geometry.flip_triangles = True
        geometry.scale = 2.0
        self.assertIs(geometry.flip_triangles, True)
        self.assertEqual(geometry.scale, 2.0)
        self.assertNotEqual(model.compile().files["models/test/box.vvd"], before["models/test/box.vvd"])
        with self.assertRaises(TypeError):
            geometry.flip_triangles = None

    def test_rest_pose_takes_a_quaternion_or_euler_angles(self):
        model, mesh, root = box_model()
        geometry = mesh.geometry
        geometry.set_rest_pose(root, (1, 2, 3), rotation=(0, 0, 0.7071068, 0.7071068))
        geometry.set_rest_pose("root", (1, 2, 3), euler=(0, 0, 1.5707964))
        with self.assertRaises(TypeError):
            geometry.set_rest_pose(root, rotation=(0, 0, 0, 1), euler=(0, 0, 0))
        with self.assertRaises(smdl.NotFoundError):
            geometry.set_rest_pose("missing")
        self.assertEqual(model.compile().files["models/test/box.mdl"][:4], b"IDST")

    def test_append_geometry_is_merged_into_the_mesh(self):
        model, mesh, root = box_model()
        extra = mesh.append_geometry()
        self.assertIsInstance(extra, smdl.Geometry)
        self.assertNotEqual(extra, mesh.geometry)
        fill_box(extra, 4.0, "extra")
        extra.set_append_offset((20, 0, 0), (0, 90, 0), 0.5)
        self.assertEqual(model.compile().files["models/test/box.mdl"][:4], b"IDST")

    def test_wrong_array_shapes_raise_clear_errors(self):
        model, mesh, root = box_model()
        geometry = mesh.geometry
        with self.assertRaisesRegex(ValueError, "positions rows must have 3 values"):
            geometry.set_positions([(0, 0)] * 4)
        with self.assertRaisesRegex(ValueError, "indices rows must have 3 values"):
            geometry.set_triangles([(0, 1)])
        with self.assertRaisesRegex(ValueError, "indices rows must have 4 values"):
            geometry.set_quads([(0, 1, 2)])
        with self.assertRaises(smdl.InvalidArgumentError) as caught:
            geometry.set_normals([(0, 0, 1)] * 5)
        self.assertIn("5 entries", str(caught.exception))
        with self.assertRaisesRegex(ValueError, "one id per triangle"):
            geometry.set_triangles([(0, 1, 2)] * 12, [0])
        with self.assertRaisesRegex(ValueError, "2 influences per vertex but weights has 1"):
            geometry.set_weights([[0, 0]] * 24, [[1.0]] * 24)
        with self.assertRaisesRegex(ValueError, "one row per vertex"):
            geometry.set_weights([[0]] * 23, [[1.0]] * 24)
        with self.assertRaisesRegex(ValueError, "2-D"):
            geometry.set_weights(memoryview(bytes(96)).cast("I"), memoryview(bytes(96)).cast("f"))
        with self.assertRaises(smdl.InvalidArgumentError):
            geometry.set_weights([[0]] * 23, [[1.0]] * 23)

    @unittest.skipIf(numpy is None, "numpy is not installed")
    def test_weights_take_at_most_16_influences_per_vertex(self):
        model, mesh, root = box_model()
        geometry = mesh.geometry
        geometry.set_weights(numpy.zeros((24, 16), dtype=numpy.uint32),
                             numpy.full((24, 16), 1 / 16, dtype=numpy.float32))
        with self.assertRaises(smdl.InvalidArgumentError) as caught:
            geometry.set_weights(numpy.zeros((24, 17), dtype=numpy.uint32), numpy.zeros((24, 17), dtype=numpy.float32))
        self.assertIn("must be 1..16, got 17", str(caught.exception))

    def test_ids_out_of_range_and_duplicates_raise_the_matching_errors(self):
        model, mesh, root = box_model()
        geometry = mesh.geometry
        with self.assertRaises(smdl.OutOfRangeError):
            geometry.set_weights([[7]] * 24, [[1.0]] * 24)
        with self.assertRaises(smdl.OutOfRangeError):
            geometry.set_triangles([(0, 1, 99)])
        with self.assertRaises(smdl.OutOfRangeError):
            geometry.set_triangles([(0, 1, 2)], [5])
        with self.assertRaises(smdl.OutOfRangeError):
            geometry.set_uvs([(0, 0)] * 24, 9)
        with self.assertRaises(smdl.DuplicateNameError):
            geometry.add_material("BOXMAT")
        with self.assertRaises(TypeError):
            geometry.set_positions("positions")

    def test_failed_setter_leaves_the_geometry_usable(self):
        model, mesh, root = box_model()
        with self.assertRaises(smdl.OutOfRangeError):
            mesh.geometry.set_triangles([(0, 1, 99)])
        self.assertEqual(mesh.geometry.triangle_count, 12)
        self.assertEqual(model.compile().files["models/test/box.mdl"][:4], b"IDST")

    def test_geometry_views_are_equal_for_the_same_object(self):
        model, mesh, root = box_model()
        other = model.create_mesh("other")
        self.assertEqual(mesh.geometry, mesh.geometry)
        self.assertEqual(hash(mesh.geometry), hash(mesh.geometry))
        self.assertNotEqual(mesh.geometry, other.geometry)
        self.assertIs(mesh.geometry.model, model)
        with self.assertRaises(TypeError):
            smdl.Geometry()


@unittest.skipIf(smdl is None, "pylib.studiomdl is not built for this platform")
class ShapeAndFlexTests(unittest.TestCase):
    def shaped_model(self):
        model, mesh, root = box_model()
        top = mesh.geometry.add_shape("raise_top", [4, 5, 6, 7], [(0, 0, 8)] * 4)
        return model, mesh, root, top

    def test_shape_flex_controller_and_rule_compile(self):
        model, mesh, root, top = self.shaped_model()
        plain = model.compile().files["models/test/box.mdl"]
        key = mesh.add_flex("raise_top", top)
        controller = model.add_flex_controller("raise", "box", 0.0, 1.0)
        (flex,) = key.flexes
        model.add_flex_rule(flex, "min(raise * 2, 1)")
        result = model.compile()
        self.assertEqual(warnings_and_errors(result), [])
        self.assertGreater(len(result.files["models/test/box.mdl"]), len(plain))
        self.assertEqual((model.flex_count, model.flex_controller_count), (1, 1))
        self.assertEqual((controller.name, controller.group, controller.min, controller.max),
                         ("raise", "box", 0.0, 1.0))

    def test_shapes_are_views_with_names_and_lookups(self):
        model, mesh, root, top = self.shaped_model()
        geometry = mesh.geometry
        self.assertEqual((top.name, top.index), ("raise_top", 0))
        self.assertEqual(top.geometry, geometry)
        self.assertEqual(geometry.find_shape("raise_top"), top)
        self.assertEqual(geometry.shapes, (top,))
        self.assertEqual(geometry.shape_count, 1)
        with self.assertRaises(smdl.NotFoundError):
            geometry.find_shape("missing")
        with self.assertRaises(smdl.DuplicateNameError):
            geometry.add_shape("raise_top", [0], [(0, 0, 1)])

    def test_flexes_accept_a_shape_its_name_its_index_or_none_for_the_base(self):
        model, mesh, root, top = self.shaped_model()
        for shape in (top, "raise_top", 0, None):
            mesh.add_flex("flex%s" % (shape if not isinstance(shape, smdl.Shape) else "_obj"), shape)
        with self.assertRaises(smdl.NotFoundError):
            mesh.add_flex("bad", "missing")
        with self.assertRaises(TypeError):
            mesh.add_flex("bad", 1.5)

    def test_shape_of_another_geometry_is_rejected(self):
        model, mesh, root, top = self.shaped_model()
        other = model.create_mesh("other")
        fill_box(other.geometry)
        foreign = other.geometry.add_shape("foreign", [0], [(0, 0, 1)])
        with self.assertRaisesRegex(ValueError, "another geometry"):
            mesh.add_flex("f", foreign)

    def test_invalid_shape_data_raises(self):
        model, mesh, root, top = self.shaped_model()
        geometry = mesh.geometry
        with self.assertRaisesRegex(ValueError, "2 vertices but 1 position deltas"):
            geometry.add_shape("a", [0, 1], [(0, 0, 1)])
        with self.assertRaisesRegex(ValueError, "normal deltas"):
            geometry.add_shape("a", [0, 1], [(0, 0, 1)] * 2, normal_deltas=[(0, 0, 1)])
        with self.assertRaises(smdl.OutOfRangeError):
            geometry.add_shape("a", [0, 99], [(0, 0, 1)] * 2)
        shape = geometry.add_shape("b", [0, 1], [(0, 0, 1)] * 2, normal_deltas=[(0, 0, 1)] * 2, wrinkle=[0.5, 0.2])
        with self.assertRaises(smdl.InvalidArgumentError):
            shape.set_normal_deltas([(0, 0, 1)])
        with self.assertRaises(smdl.InvalidArgumentError):
            shape.set_wrinkle([0.1])
        shape.set_normal_deltas([(0, 1, 0)] * 2)
        shape.set_wrinkle([0.3, 0.4])

    def test_flex_pair_and_key_options(self):
        model, mesh, root, top = self.shaped_model()
        pair = mesh.add_flex_pair("smile", 0.4, top)
        right, left = pair.flexes
        self.assertEqual((right.name, left.name), ("smileR", "smileL"))
        self.assertEqual(model.find_flex("smileL"), left)
        self.assertIsNone(pair.position)
        pair.position = 0.5
        pair.decay = 0.25
        pair.split = 0.3
        self.assertEqual((pair.position, pair.decay, pair.split), (0.5, 0.25, 0.30000001192092896))
        single = mesh.add_flex("raise", top)
        self.assertEqual(len(single.flexes), 1)
        self.assertEqual(single.name, "raise")
        controller = model.add_flex_controller("c")
        model.add_flex_rule(right, "c")
        model.add_flex_rule("smileL", "c * 0.5")
        self.assertEqual(model.compile().files["models/test/box.mdl"][:4], b"IDST")

    def test_flex_rules_report_expression_and_name_errors(self):
        model, mesh, root, top = self.shaped_model()
        (flex,) = mesh.add_flex("raise", top).flexes
        model.add_flex_controller("c")
        with self.assertRaises(smdl.ExpressionSyntaxError):
            model.add_flex_rule(flex, "((")
        with self.assertRaises(smdl.NotFoundError):
            model.add_flex_rule(flex, "unknown_controller + 1")
        with self.assertRaises(smdl.NotFoundError):
            model.add_flex_rule("no_such_flex", "c")
        with self.assertRaises(smdl.DuplicateNameError):
            model.add_flex_controller("c")
        with self.assertRaises(smdl.NotFoundError):
            model.find_flex_controller("missing")
        with self.assertRaises(TypeError):
            model.add_flex_rule(5, "c")

    def test_eyeballs_eyelids_mouth_and_bone_flex_driver_compile(self):
        model, mesh, root, top = self.shaped_model()
        geometry = mesh.geometry
        geometry.add_shape("lowered", [0, 1, 2, 3], [(0, 0, -1)] * 4)
        jaw = model.add_bone("jaw", root, (0, 0, 2))
        eyeball = mesh.add_eyeball("eye", root, (0, 0, 5), "boxmat", 2.0, z_angle=4.0)
        self.assertEqual((eyeball.name, eyeball.index, eyeball.mesh), ("eye", 0, mesh))
        lid = mesh.add_eyelid("upper_right", smdl.EyelidKind.UPPER)
        lid.set_state(smdl.EyelidState.LOWERER, "lowered", -0.5)
        lid.set_state(smdl.EyelidState.NEUTRAL, None, 0.0)
        lid.set_state(smdl.EyelidState.RAISER, top, 0.5)
        lid.split = 0.1
        lid.set_eyeball("eye")
        lid.set_eyeball(eyeball)
        lid.set_eyeball()
        self.assertAlmostEqual(lid.split, 0.1, places=6)
        mesh.add_flex("raise", top)
        mesh.set_default_flex()
        mesh.add_sphere_normals("boxmat", (0, 0, 0))
        model.add_local_var("amount")
        model.set_mouth(0, "mouth_open", jaw, (0, 1, 0))
        controller = model.add_flex_controller("c", "eyes", -1.0, 1.0)
        model.add_bone_flex_driver(jaw, smdl.BoneFlexComponent.TZ, controller, -1.0, 1.0)
        model.add_bone_flex_driver("jaw", smdl.BoneFlexComponent.TX, "c", -1.0, 1.0)
        model.add_flex_rule("raise", "c")
        model.add_flex_rule(model.find_flex("amount"), "c * 0.5")
        result = model.compile()
        self.assertEqual(warnings_and_errors(result), [])
        with self.assertRaises(smdl.NotFoundError):
            lid.set_eyeball("missing")
        other = model.create_mesh("other")
        fill_box(other.geometry)
        foreign = other.add_eyeball("foreign", root, (0, 0, 0), "boxmat", 1.0)
        with self.assertRaisesRegex(ValueError, "another mesh"):
            lid.set_eyeball(foreign)

    def test_combination_operator_compiles(self):
        model, mesh, root = box_model("test/combo")
        geometry = mesh.geometry
        geometry.add_shape("a", [0, 1, 2, 3], [(0, 0, 1)] * 4)
        geometry.add_shape("b", [4, 5, 6, 7], [(0, 0, 1)] * 4)
        geometry.add_shape("a_b", [0, 4], [(0, 0, 1)] * 2)
        control = mesh.add_combination_control("ab", ["a", "b"])
        self.assertEqual(control.name, "ab")
        control.set_range(0.0, 1.0)
        mesh.add_combination_dominator(["a"], ["b"])
        self.assertEqual(model.compile().files["models/test/combo.mdl"][:4], b"IDST")
        with self.assertRaises(TypeError):
            mesh.add_combination_control("c", "xy")
        with self.assertRaises(smdl.DuplicateNameError):
            mesh.add_combination_control("ab", ["z"])

    def test_combination_control_without_raw_controls_feeds_flex_rules(self):
        model, mesh, root = box_model("test/combo_rules")
        mesh.geometry.add_shape("open", [0, 1, 2, 3], [(0, 0, 1)] * 4)
        mesh.add_combination_control("jaw", []).set_range(0.0, 2.0)
        with self.assertRaises(smdl.CompileError):  # the controller needs a rule to feed
            model.compile()
        mesh.add_combination_expression("open", "jaw * 0.5")
        self.assertEqual(model.compile().files["models/test/combo_rules.mdl"][:4], b"IDST")

    def test_vertex_cache_animation_compiles(self):
        model, mesh, root = box_model("test/vca")
        geometry = mesh.geometry
        geometry.add_shape("f1", [0, 1, 2, 3], [(0, 0, 1)] * 4)
        geometry.add_shape("f2", [4, 5, 6, 7], [(0, 0, 1)] * 4)
        mesh.add_vertex_cache_animation("vca", ["f1", 1])
        self.assertEqual(model.compile().files["models/test/vca.mdl"][:4], b"IDST")
        with self.assertRaises(TypeError):
            mesh.add_vertex_cache_animation("vca2", "f1")


@unittest.skipIf(smdl is None, "pylib.studiomdl is not built for this platform")
class BodyGroupAndLodTests(unittest.TestCase):
    def test_body_group_with_a_blank_compiles(self):
        model, root = new_model()
        body = model.create_mesh("body")
        fill_box(body.geometry)
        lid = model.add_body_group("lid")
        lid_a = lid.create_mesh("lid_a")
        fill_box(lid_a.geometry, 4.0)
        lid.add_blank()
        self.assertEqual((lid.name, lid.choice_count, lid.choices), ("lid", 2, (lid_a, None)))
        self.assertEqual((model.body_group_count, model.find_body_group("lid"), model.find_mesh("lid_a")),
                         (2, lid, lid_a))
        self.assertEqual(lid_a.name, "lid_a")
        result = model.compile()
        self.assertEqual(result.files["models/test/box.mdl"][:4], b"IDST")
        self.assertEqual(warnings_and_errors(result), [])

    def test_body_group_presets_take_a_dict_or_pairs(self):
        model, root = new_model()
        body = model.create_mesh("body")
        fill_box(body.geometry)
        lid = model.add_body_group("lid")
        fill_box(lid.create_mesh("lid_a").geometry, 4.0)
        lid.add_blank()
        model.add_body_group_preset("off", {lid: 1, "body": 0})
        model.add_body_group_preset("on", [(lid, 0)])
        with self.assertRaises(smdl.DuplicateNameError):
            model.add_body_group_preset("on", [(lid, 0)])
        with self.assertRaises(TypeError):
            model.add_body_group_preset("bad", [(lid,)])
        with self.assertRaises(smdl.NotFoundError):
            model.add_body_group_preset("bad", {"missing": 0})
        self.assertEqual(model.compile().files["models/test/box.mdl"][:4], b"IDST")

    def test_lookups_and_duplicates_raise(self):
        model, root = new_model()
        model.create_mesh("body")
        with self.assertRaises(smdl.NotFoundError):
            model.find_mesh("missing")
        with self.assertRaises(smdl.NotFoundError):
            model.find_body_group("missing")
        with self.assertRaises(smdl.DuplicateNameError):
            model.create_mesh("body")
        with self.assertRaises(TypeError):
            smdl.Mesh()

    def test_lods_replace_and_remove_meshes(self):
        model, root = new_model()
        body = model.create_mesh("body")
        fill_box(body.geometry)
        lid = model.add_body_group("lid")
        lid_a = lid.create_mesh("lid_a")
        fill_box(lid_a.geometry, 4.0)
        lod = model.add_lod(10.0)
        self.assertFalse(lod.is_shadow)
        replacement = lod.replace_mesh("body")
        fill_box(replacement, 10.0)
        lod.remove_mesh(lid_a)
        lod.facial_animation = False
        self.assertIs(lod.facial_animation, False)
        shadow = model.add_shadow_lod()
        self.assertTrue(shadow.is_shadow)
        shared = shadow.replace_mesh(body, replacement)
        self.assertEqual(shared, replacement)
        self.assertEqual(model.lod_count, 2)
        result = model.compile()
        self.assertEqual(result.files["models/test/box.mdl"][:4], b"IDST")

    def test_lod_bone_and_material_commands(self):
        model, root = new_model()
        arm = model.add_bone("arm", root, (0, 0, 5))
        body = model.create_mesh("body")
        fill_box(body.geometry)
        body.geometry.set_weights([[arm.index]] * 24, [[1.0]] * 24)
        lod = model.add_lod(5.0)
        lod.replace_bone(arm, "root")
        lod.collapse_bone_tree(root)
        lod.replace_material("boxmat", "boxmat2")
        lod.remove_material("nothing")
        with self.assertRaises(smdl.NotFoundError):
            lod.replace_bone("missing", root)
        with self.assertRaises(smdl.NotFoundError):
            lod.remove_mesh("missing")
        with self.assertRaisesRegex(ValueError, "another model"):
            lod.replace_mesh(body, model_other_geometry())

    def test_lod_rules_are_enforced(self):
        model, root = new_model()
        model.create_mesh("body")
        shadow = model.add_shadow_lod()
        with self.assertRaises(smdl.InvalidArgumentError):
            shadow.facial_animation = True
        with self.assertRaises(smdl.InvalidArgumentError):
            model.add_lod(3.0)
        with self.assertRaises(smdl.DuplicateNameError):
            model.add_shadow_lod()
        lod_model, _ = new_model()
        regular = lod_model.add_lod(3.0)
        with self.assertRaises(smdl.InvalidArgumentError):
            regular.use_shadow_lod_materials = True


def model_other_geometry():
    other, _ = new_model("test/other")
    return other.create_mesh("o").geometry


if __name__ == "__main__":
    unittest.main()
