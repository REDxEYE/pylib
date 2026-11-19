"""pylib.studiomdl collision model and bone dynamics (jiggle bones, procedural bones).

pylib.studiomdl only exists in x86_64 builds (studiomdl_v2 uses x86 intrinsics); the tests skip elsewhere.
"""
import unittest

try:
    import pylib.studiomdl as smdl
except ImportError:  # arm64 macOS build
    smdl = None

SKIP = "pylib.studiomdl is not built for this platform"


def skeleton_model(name="props/dyn"):
    model = smdl.Model(name)
    model.create_sequence("idle").add_animation(model.create_animation("idle_anim", 30, 1))
    root = model.add_bone("root")
    arm = model.add_bone("arm", root, (0, 0, 10))
    hand = model.add_bone("hand", arm, (0, 0, 10))
    return model, root, arm, hand


@unittest.skipIf(smdl is None, SKIP)
class CollisionSettingsTests(unittest.TestCase):
    def test_collision_is_one_object_per_model(self):
        model = smdl.Model("a")
        self.assertEqual(model.collision, model.collision)
        self.assertIs(model.collision.model, model)
        self.assertNotEqual(smdl.Model("a").collision, model.collision)

    def test_settings_read_back_the_assigned_value_and_default_to_none(self):
        collision = smdl.Model("a").collision
        self.assertIsNone(collision.mass)
        collision.mass = 50
        collision.jointed = True
        collision.mass_center = (1, 2, 3)
        collision.max_convex_pieces = 4
        collision.geometry  # the collision model's own geometry allows a source name
        collision.source_name = "src/phys_box"
        self.assertEqual(collision.mass, 50.0)
        self.assertIs(collision.jointed, True)
        self.assertEqual(collision.mass_center, (1.0, 2.0, 3.0))
        self.assertEqual(collision.max_convex_pieces, 4)
        self.assertEqual(collision.source_name, "src/phys_box")
        with self.assertRaises(TypeError):
            collision.mass = None
        with self.assertRaises(TypeError):
            collision.damping = "high"
        with self.assertRaises(ValueError):
            collision.mass_center = (1, 2)
        self.assertIsNone(collision.damping)

    def test_root_bone_takes_a_bone_or_a_name(self):
        model, root, arm, hand = skeleton_model()
        collision = model.collision
        self.assertIsNone(collision.root_bone)
        collision.root_bone = arm
        self.assertEqual(collision.root_bone, arm)
        collision.root_bone = "hand"
        self.assertEqual(collision.root_bone, hand)
        with self.assertRaises(TypeError):
            collision.root_bone = None
        with self.assertRaises(KeyError):
            collision.root_bone = "nope"
        with self.assertRaises(ValueError):
            collision.root_bone = smdl.Model("other").add_bone("root")
        self.assertEqual(collision.root_bone, hand)

    def test_joint_commands_validate_their_arguments(self):
        model, root, arm, hand = skeleton_model()
        collision = model.collision
        collision.joint_constrain(arm, smdl.CollisionAxis.X, smdl.CollisionJointType.LIMIT, -45, 45, friction=2)
        collision.joint_constrain("hand", smdl.CollisionAxis.Y, smdl.CollisionJointType.FIXED)
        collision.joint_set_inertia(arm, 2)
        collision.joint_set_damping(arm, 0.5)
        collision.joint_set_rot_damping(arm, 0.5)
        collision.joint_set_mass_bias(arm, 2)
        collision.joint_collide(root, "hand")
        collision.joint_no_collide(root, hand)
        collision.joint_collide_all([root, arm, "hand"])
        collision.joint_skip(hand)
        collision.joint_merge(arm, "hand")
        with self.assertRaises(smdl.InvalidArgumentError):
            collision.joint_constrain(arm, smdl.CollisionAxis.Z, smdl.CollisionJointType.LIMIT, 10, -10)
        with self.assertRaises(TypeError):
            collision.joint_constrain(arm, "x", smdl.CollisionJointType.FREE)
        with self.assertRaises(TypeError):
            collision.joint_skip(None)
        with self.assertRaises(KeyError):
            collision.joint_set_inertia("nope", 1)
        with self.assertRaises(ValueError):
            collision.joint_collide(arm, smdl.Model("other").add_bone("arm"))
        with self.assertRaises(TypeError):
            collision.joint_collide_all(arm)
        with self.assertRaises(smdl.LimitError):
            collision.joint_collide_all([root] * 33)

    def test_whole_model_commands_and_friction(self):
        collision = smdl.Model("a").collision
        for command in (collision.auto_mass, collision.remove_2d, collision.concave_per_joint, collision.concave,
                        collision.convex_hull_count_override, collision.no_self_collisions,
                        collision.assume_world_space):
            command()
        collision.animated_friction(10, 20, 0.5, 1, 0.5)
        with self.assertRaises(TypeError):
            collision.animated_friction(10, 20)
        with self.assertRaises(TypeError):
            collision.auto_mass(1)

    def test_enum_members(self):
        self.assertEqual([m.name for m in smdl.CollisionJointType], ["FREE", "FIXED", "LIMIT"])
        self.assertEqual([m.name for m in smdl.CollisionAxis], ["X", "Y", "Z"])

    def test_views_cannot_be_constructed(self):
        for view in (smdl.Collision, smdl.ConvexSource, smdl.JiggleBone, smdl.QuatInterp, smdl.AimConstraint,
                     smdl.AxisInterp):
            with self.assertRaises(TypeError):
                view()


@unittest.skipIf(smdl is None, SKIP)
class JiggleBoneTests(unittest.TestCase):
    def test_sections_and_parameters_read_back(self):
        model, root, arm, hand = skeleton_model()
        self.assertEqual(model.jiggle_bone_count, 0)
        jiggle = model.add_jiggle_bone(arm)
        self.assertEqual(model.jiggle_bone_count, 1)
        self.assertIsNone(jiggle.length)
        jiggle.flexible = True
        jiggle.base_spring = True
        jiggle.length = 12
        jiggle.yaw_constraint = (-30, 30)
        jiggle.yaw_stiffness = 200
        jiggle.base_left_constraint = [-1, 1]
        jiggle.base_mass = 3
        self.assertIs(jiggle.flexible, True)
        self.assertEqual(jiggle.length, 12.0)
        self.assertEqual(jiggle.yaw_constraint, (-30.0, 30.0))
        self.assertEqual(jiggle.base_left_constraint, (-1.0, 1.0))
        self.assertEqual(jiggle.base_mass, 3.0)
        self.assertIsNone(jiggle.pitch_friction)
        self.assertIs(jiggle.model, model)

    def test_wrong_arguments_are_rejected(self):
        model, root, arm, hand = skeleton_model()
        jiggle = model.add_jiggle_bone("arm")
        with self.assertRaises(smdl.DuplicateNameError):
            model.add_jiggle_bone(arm)
        with self.assertRaises(KeyError):
            model.add_jiggle_bone("nope")
        with self.assertRaises(TypeError):
            model.add_jiggle_bone(None)
        with self.assertRaises(TypeError):
            jiggle.yaw_constraint = 10
        with self.assertRaises(TypeError):
            jiggle.yaw_constraint = (1, 2, 3)
        with self.assertRaises(TypeError):
            jiggle.length = None
        jiggle.flexible = True
        with self.assertRaises(smdl.InvalidArgumentError):
            jiggle.rigid = True
        self.assertIsNone(jiggle.rigid)

    def test_views_of_one_jiggle_bone_share_nothing_with_another(self):
        model, root, arm, hand = skeleton_model()
        first = model.add_jiggle_bone(arm)
        second = model.add_jiggle_bone(hand)
        first.length = 5
        self.assertIsNone(second.length)
        self.assertNotEqual(first, second)


@unittest.skipIf(smdl is None, SKIP)
class ProceduralBoneTests(unittest.TestCase):
    def test_quat_interp_triggers(self):
        model, root, arm, hand = skeleton_model()
        helper = model.add_quat_interp(hand, arm, root, "arm")
        helper.set_display((1, 2, 3), 50)
        helper.base_position = (0, 0, 1)
        helper.rotate_axis = (0, 90, 0)
        helper.joint_orient = (0, 0, 90)
        helper.add_trigger(45, (0, 0, 0), (0, 0, 0), (0, 0, 0))
        helper.add_trigger(tolerance=45, trigger=(90, 0, 0), angles=(0, 10, 0), position=(1, 0, 0))
        self.assertEqual(helper.base_position, (0.0, 0.0, 1.0))
        with self.assertRaises(TypeError):
            helper.add_trigger(45, (0, 0, 0), (0, 0, 0))
        with self.assertRaises(ValueError):
            helper.rotate_axis = (1, 2)
        with self.assertRaises(smdl.InvalidArgumentError):
            helper.add_trigger(0, (0, 0, 0), (0, 0, 0), (0, 0, 0))
        with self.assertRaises(smdl.LimitError):
            for _ in range(40):
                helper.add_trigger(10, (0, 0, 0), (0, 0, 0), (0, 0, 0))
        with self.assertRaises(KeyError):
            model.add_quat_interp(hand, arm, root, "nope")

    def test_aim_constraint_needs_exactly_one_target(self):
        model, root, arm, hand = skeleton_model()
        aim = model.add_aim_constraint(arm, root, aim_bone=hand)
        aim.aim_vector = (1, 0, 0)
        aim.up_vector = (0, 0, 1)
        aim.base_position = (0, 0, 2)
        self.assertEqual(aim.aim_vector, (1.0, 0.0, 0.0))
        model.add_aim_constraint("hand", "arm", "root")
        with self.assertRaises(TypeError):
            model.add_aim_constraint(arm, root)
        with self.assertRaises(TypeError):
            model.add_aim_constraint(arm, root, hand, aim_attachment=0)
        with self.assertRaises(TypeError):
            model.add_aim_constraint(arm, root, aim_attachment=None)
        with self.assertRaises(TypeError):
            model.add_aim_constraint(arm, root, aim_attachment=1.5)

    def test_axis_interp_orientations(self):
        model, root, arm, hand = skeleton_model()
        axis = model.add_axis_interp(arm, root, 1)
        axis.base_position = (1, 2, 3)
        axis.set_orientation(0, (0, 0, 0), (0, 0, 0))
        axis.set_orientation(index=5, position=(1, 0, 0), angles=(0, 90, 0))
        self.assertEqual(axis.base_position, (1.0, 2.0, 3.0))
        with self.assertRaises(smdl.OutOfRangeError):
            axis.set_orientation(6, (0, 0, 0), (0, 0, 0))
        with self.assertRaises(smdl.OutOfRangeError):
            model.add_axis_interp(arm, root, 4)


BOX_POSITIONS = [(x, y, z) for z in (0, 10) for y in (0, 10) for x in (0, 10)]
BOX_TRIANGLES = [(0, 2, 1), (1, 2, 3), (4, 5, 6), (5, 7, 6), (0, 1, 4), (1, 5, 4),
                 (2, 6, 3), (3, 6, 7), (0, 4, 2), (2, 4, 6), (1, 3, 5), (3, 7, 5)]


BOX_BONES = (0, 0, 0, 0, 1, 1, 2, 2)  # every bone must be used or studiomdl collapses it


def fill_box(geometry, material="box"):
    geometry.set_positions(BOX_POSITIONS)
    geometry.set_normals([(0, 0, 1)] * len(BOX_POSITIONS))
    geometry.set_uvs([(0, 0)] * len(BOX_POSITIONS))
    geometry.set_weights([(bone,) for bone in BOX_BONES], [(1.0,)] * len(BOX_POSITIONS))
    geometry.add_material(material)
    geometry.set_triangles(BOX_TRIANGLES)


def box_model(name="props/dynbox"):
    model, root, arm, hand = skeleton_model(name)
    model.add_material_path("models/test")
    fill_box(model.create_mesh("body").geometry)
    return model, root, arm, hand


@unittest.skipIf(smdl is None, SKIP)
class DynamicsCompileTests(unittest.TestCase):
    def phy(self, model):
        result = model.compile()
        phys = [key for key in result.files if key.endswith(".phy")]
        self.assertEqual(len(phys), 1, list(result.files))
        return result.files[phys[0]]

    def test_collision_model_writes_a_phy(self):
        model, root, arm, hand = box_model()
        self.assertFalse([k for k in model.compile().files if k.endswith(".phy")])
        collision = model.collision
        fill_box(collision.geometry)
        collision.mass = 30
        collision.source_name = "src/phys_box"
        phy = self.phy(model)
        self.assertGreater(len(phy), 16)
        self.assertIn(b"phys_box", phy)

    def test_collision_geometry_can_be_a_mesh_geometry(self):
        model, root, arm, hand = box_model()
        collision = model.collision
        self.assertIsNone(collision.mass)
        collision.geometry = model.find_mesh("body").geometry
        self.assertEqual(collision.geometry, model.find_mesh("body").geometry)
        collision.auto_mass()
        self.assertGreater(len(self.phy(model)), 16)

    def test_geometry_of_another_model_is_rejected(self):
        model, root, arm, hand = box_model()
        other, *_ = box_model()
        with self.assertRaises(ValueError):
            model.collision.geometry = other.find_mesh("body").geometry
        with self.assertRaises(TypeError):
            model.collision.geometry = "body"
        with self.assertRaises(TypeError):
            model.collision.geometry = None

    def test_source_name_needs_the_collision_models_own_geometry(self):
        model, root, arm, hand = box_model()
        model.collision.geometry = model.find_mesh("body").geometry
        with self.assertRaises(smdl.InvalidArgumentError):
            model.collision.source_name = "src/phys_box"

    def test_jointed_collision_with_constraints(self):
        model, root, arm, hand = box_model("props/joints")
        collision = model.collision
        collision.jointed = True
        geometry = collision.geometry
        positions = [(x, y, z + 10 * bone) for bone in range(3) for (x, y, z) in BOX_POSITIONS]
        geometry.set_positions(positions)
        geometry.set_normals([(0, 0, 1)] * len(positions))
        geometry.set_weights([(i // 8,) for i in range(len(positions))], [(1.0,)] * len(positions))
        geometry.add_material("box")
        geometry.set_triangles([tuple(i + 8 * bone for i in tri) for bone in range(3) for tri in BOX_TRIANGLES])
        collision.joint_constrain(arm, smdl.CollisionAxis.X, smdl.CollisionJointType.LIMIT, -45, 45)
        collision.joint_constrain(hand, smdl.CollisionAxis.Y, smdl.CollisionJointType.FIXED)
        collision.joint_set_inertia(arm, 2)
        collision.joint_collide(root, hand)
        collision.damping = 0.5
        collision.root_bone = root
        self.assertGreater(len(self.phy(model)), 16)

    def test_collision_commands_without_geometry_fail_the_compile(self):
        model, root, arm, hand = skeleton_model("props/nogeometry")
        model.collision.joint_skip(arm)
        with self.assertRaises(smdl.CompileError):
            model.compile()

    def test_convex_sources(self):
        model, root, arm, hand = box_model()
        collision = model.collision
        self.assertEqual(collision.convex_sources, ())
        first = collision.add_convex_source(name="src/convex_a")
        fill_box(first.geometry)
        first.set_offset(position=(0, 0, 5), angles=(0, 90, 0), scale=2)
        first.concave = True
        second = collision.add_convex_source(model.find_mesh("body").geometry)
        self.assertEqual((first.index, second.index), (0, 1))
        self.assertEqual(collision.convex_sources, (first, second))
        self.assertEqual(collision.convex_sources[0].geometry, first.geometry)
        self.assertEqual(second.geometry, model.find_mesh("body").geometry)
        self.assertIs(first.concave, True)
        self.assertEqual(first.name, "src/convex_a")
        with self.assertRaises(ValueError):
            collision.add_convex_source(model.find_mesh("body").geometry, name="x")
        with self.assertRaises(smdl.InvalidArgumentError):
            second.name = "other"
        with self.assertRaises(TypeError):
            first.concave = None
        self.assertGreater(len(self.phy(model)), 16)

    def test_convex_source_limit(self):
        model, root, arm, hand = box_model()
        collision = model.collision
        with self.assertRaises(smdl.LimitError):
            for _ in range(30):
                collision.add_convex_source()
        self.assertEqual(len(collision.convex_sources), 24)

    def test_phy_text_section(self):
        model, root, arm, hand = box_model()
        collision = model.collision
        fill_box(collision.geometry)
        plain = self.phy(model)
        collision.text = '"solid"\n{\n"index" "0"\n"mass" "123"\n}\n'
        self.assertNotEqual(self.phy(model), plain)

    def test_jiggle_bone_changes_the_mdl(self):
        model, root, arm, hand = box_model()
        plain = model.compile().files["models/props/dynbox.mdl"]
        jiggle = model.add_jiggle_bone(hand)
        jiggle.flexible = True
        jiggle.length = 20
        jiggle.yaw_stiffness = 150
        jiggle.yaw_constraint = (-30, 30)
        compiled = model.compile().files["models/props/dynbox.mdl"]
        self.assertTrue(compiled.startswith(b"IDST"))
        self.assertNotEqual(compiled, plain)

    def test_rigid_and_base_spring_jiggle_bones_compile(self):
        model, root, arm, hand = box_model()
        rigid = model.add_jiggle_bone(arm)
        rigid.rigid = True
        rigid.tip_mass = 2
        rigid.angle_constraint = 20
        base = model.add_jiggle_bone(hand)
        base.base_spring = True
        base.base_stiffness = 50
        base.base_left_constraint = (-5, 5)
        base.base_mass = 2
        self.assertTrue(model.compile().files["models/props/dynbox.mdl"].startswith(b"IDST"))

    def test_quat_interp_with_triggers_compiles(self):
        model, root, arm, hand = box_model()
        plain = model.compile().files["models/props/dynbox.mdl"]
        helper = model.add_quat_interp(hand, arm, root, arm)
        helper.set_display((1, 1, 1), 100)
        helper.add_trigger(90, (0, 0, 0), (0, 0, 0), (0, 0, 0))
        helper.add_trigger(90, (90, 0, 0), (0, 0, 0), (0, 0, 5))
        compiled = model.compile().files["models/props/dynbox.mdl"]
        self.assertTrue(compiled.startswith(b"IDST"))
        self.assertNotEqual(compiled, plain)

    def test_aim_constraint_to_an_attachment_compiles(self):
        model, root, arm, hand = box_model()
        plain = model.compile().files["models/props/dynbox.mdl"]
        tip = model.add_attachment("tip", hand, (0, 0, 5))
        for target in (tip, "tip", tip.index):
            aim = model.add_aim_constraint(arm, root, aim_attachment=target)
            aim.aim_vector = (1, 0, 0)
            aim.up_vector = (0, 0, 1)
            aim.base_position = (0, 0, 1)
            compiled = model.compile().files["models/props/dynbox.mdl"]
            self.assertTrue(compiled.startswith(b"IDST"))
            self.assertNotEqual(compiled, plain)
            model, root, arm, hand = box_model()
            tip = model.add_attachment("tip", hand, (0, 0, 5))

    def test_aim_attachment_must_belong_to_the_model(self):
        model, root, arm, hand = box_model()
        other, other_root, other_arm, other_hand = box_model()
        foreign = other.add_attachment("tip", other_hand)
        with self.assertRaises(ValueError):
            model.add_aim_constraint(arm, root, aim_attachment=foreign)
        with self.assertRaises(KeyError):
            model.add_aim_constraint(arm, root, aim_attachment="nope")

    def test_axis_interp_compiles(self):
        model, root, arm, hand = box_model()
        axis = model.add_axis_interp(hand, arm, 1)
        axis.set_orientation(0, (0, 0, 0), (0, 0, 0))
        axis.set_orientation(1, (0, 0, 2), (0, 45, 0))
        self.assertTrue(model.compile().files["models/props/dynbox.mdl"].startswith(b"IDST"))


if __name__ == "__main__":
    unittest.main()
