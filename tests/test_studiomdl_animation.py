"""pylib.studiomdl rig (attachments, hitboxes, pose parameters, IK chains, weight lists), animations and sequences.

pylib.studiomdl only exists in x86_64 builds (studiomdl_v2 uses x86 intrinsics); the tests skip elsewhere.
"""
import array
import unittest

try:
    import pylib.studiomdl as smdl
except ImportError:  # arm64 macOS build
    smdl = None

try:
    import numpy
except ImportError:
    numpy = None

SKIP = "pylib.studiomdl is not built for this platform"

TRIANGLE = {
    "positions": [(0, 0, 0), (0, 10, 0), (0, 0, 10)],
    "normals": [(-1, 0, 0)] * 3,
    "uvs": [(0, 0), (1, 0), (0, 1)],
}


def skeleton_model(name="props/anim"):
    model = smdl.Model(name)
    root = model.add_bone("root")
    arm = model.add_bone("arm", root, (0, 0, 10))
    hand = model.add_bone("hand", arm, (0, 0, 10))
    return model, root, arm, hand


def add_triangle_mesh(model):
    geometry = model.create_mesh("body").geometry
    geometry.set_positions(TRIANGLE["positions"])
    geometry.set_normals(TRIANGLE["normals"])
    geometry.set_uvs(TRIANGLE["uvs"])
    geometry.add_material("tri")
    geometry.set_triangles([(0, 1, 2)])
    model.add_material_path("models/test")


def rest_frames(count, root_z=0.0):
    positions = [(0.0, 0.0, root_z + i) for i in range(count)]
    rotations = [(0.0, 0.0, 0.0, 1.0)] * count
    return positions, rotations


@unittest.skipIf(smdl is None, SKIP)
class AttachmentTests(unittest.TestCase):
    def test_attachments_are_found_by_name_and_remember_their_definition(self):
        model, root, arm, hand = skeleton_model()
        muzzle = model.add_attachment("muzzle", "hand", (1, 2, 3))
        self.assertEqual((muzzle.name, muzzle.index), ("muzzle", 0))
        self.assertEqual(muzzle.bone, hand)
        self.assertEqual(muzzle.position, (1.0, 2.0, 3.0))
        self.assertEqual(model.find_attachment("MUZZLE"), muzzle)
        self.assertEqual(model.find_attachment("muzzle").name, "muzzle")
        eye = model.add_attachment("eye", arm)
        self.assertEqual(model.attachments, (muzzle, eye))
        self.assertNotEqual(muzzle, eye)
        muzzle.redefine(arm, (4, 5, 6))
        self.assertEqual((muzzle.bone, muzzle.position), (arm, (4.0, 5.0, 6.0)))

    def test_flags_read_back_the_assigned_value(self):
        model, root, arm, hand = skeleton_model()
        eye = model.add_attachment("eye", root)
        self.assertIsNone(eye.rigid)
        eye.rigid = True
        eye.world_align = False
        self.assertIs(eye.rigid, True)
        self.assertIs(eye.world_align, False)
        self.assertIsNone(model.add_attachment("other", root).rigid)
        with self.assertRaises(TypeError):
            eye.rigid = None

    def test_orientation_takes_exactly_one_description(self):
        model, root, arm, hand = skeleton_model()
        eye = model.add_attachment("eye", root)
        eye.set_orientation(angles=(0, 90, 0))
        eye.set_orientation(euler=(0, 0, 1.5))
        eye.set_orientation(rotation=(0, 0, 0, 1))
        eye.set_orientation(x_axis=(1, 0, 0), z_axis=(0, 0, 1))
        eye.set_orientation(absolute=True)
        with self.assertRaises(TypeError):
            eye.set_orientation()
        with self.assertRaises(TypeError):
            eye.set_orientation(angles=(0, 0, 0), euler=(0, 0, 0))
        with self.assertRaises(TypeError):
            eye.set_orientation(x_axis=(1, 0, 0))
        with self.assertRaises(ValueError):
            eye.set_orientation(angles=(0, 0))

    def test_errors(self):
        model, root, arm, hand = skeleton_model()
        model.add_attachment("eye", root)
        with self.assertRaises(smdl.DuplicateNameError):
            model.add_attachment("eye", root)
        with self.assertRaises(smdl.NotFoundError):
            model.find_attachment("missing")
        with self.assertRaises(smdl.NotFoundError):
            model.add_attachment("a", "missing")
        with self.assertRaisesRegex(ValueError, "another model"):
            model.add_attachment("a", smdl.Model("b").add_bone("root"))
        with self.assertRaises(TypeError):
            model.add_attachment("a", None)


@unittest.skipIf(smdl is None, SKIP)
class HitboxTests(unittest.TestCase):
    def test_hitbox_sets_hold_hitboxes_in_creation_order(self):
        model, root, arm, hand = skeleton_model()
        default = model.add_hitbox_set("default")
        self.assertEqual((default.name, default.index), ("default", 0))
        self.assertEqual(default.hitboxes, ())
        first = default.add_hitbox(0, root, (-1, -1, 0), (1, 1, 10))
        second = default.add_hitbox(1, "hand", (0, 0, 0), (2, 2, 2), angles=(0, 45, 0), capsule_radius=0.5,
                                    name="palm")
        self.assertEqual(default.hitboxes, (first, second))
        self.assertEqual((first.index, second.index), (0, 1))
        self.assertEqual(second.set, default)
        self.assertEqual((second.group, second.bone), (1, hand))
        self.assertEqual((second.mins, second.maxs), ((0.0, 0.0, 0.0), (2.0, 2.0, 2.0)))
        self.assertEqual((second.angles, second.capsule_radius, second.name), ((0.0, 45.0, 0.0), 0.5, "palm"))
        self.assertIsNone(first.angles)
        first.name = "body"
        self.assertEqual(first.name, "body")
        other = model.add_hitbox_set("other")
        self.assertEqual(model.hitbox_sets, (default, other))
        self.assertEqual(model.find_hitbox_set("other"), other)
        self.assertEqual(other.hitboxes, ())
        model.add_hit_group(0, root)

    def test_errors(self):
        model, root, arm, hand = skeleton_model()
        box_set = model.add_hitbox_set("default")
        with self.assertRaises(smdl.DuplicateNameError):
            model.add_hitbox_set("DEFAULT")
        with self.assertRaises(smdl.NotFoundError):
            model.find_hitbox_set("missing")
        with self.assertRaises(ValueError):
            box_set.add_hitbox(0, root, (0, 0), (1, 1, 1))
        with self.assertRaises(TypeError):
            box_set.add_hitbox(0, root, (0, 0, 0), (1, 1, 1), name=5)


@unittest.skipIf(smdl is None, SKIP)
class PoseParameterTests(unittest.TestCase):
    def test_pose_parameters_are_found_by_name(self):
        model = smdl.Model("a")
        yaw = model.add_pose_parameter("yaw", -45, 45)
        pitch = model.add_pose_parameter("pitch", -90, 90)
        self.assertEqual((yaw.name, yaw.index, yaw.min, yaw.max), ("yaw", 0, -45.0, 45.0))
        self.assertEqual(model.pose_parameters, (yaw, pitch))
        self.assertEqual(model.find_pose_parameter("pitch"), pitch)
        with self.assertRaises(smdl.NotFoundError):
            model.find_pose_parameter("roll")
        with self.assertRaises(smdl.DuplicateNameError):
            model.add_pose_parameter("yaw", 0, 1)
        with self.assertRaises(smdl.StudioMdlError):
            model.add_pose_parameter("backwards", 1, 0)

    def test_loop_modes(self):
        model = smdl.Model("a")
        yaw = model.add_pose_parameter("yaw", 0, 360)
        self.assertIsNone(yaw.loop_mode)
        yaw.set_loop(smdl.PoseLoopMode.WRAP)
        self.assertIs(yaw.loop_mode, smdl.PoseLoopMode.WRAP)
        self.assertIsNone(yaw.loop_range)
        yaw.set_loop(smdl.PoseLoopMode.RANGE, 180)
        self.assertEqual((yaw.loop_mode, yaw.loop_range), (smdl.PoseLoopMode.RANGE, 180.0))
        yaw.set_loop(smdl.PoseLoopMode.NONE)
        self.assertIs(yaw.loop_mode, smdl.PoseLoopMode.NONE)
        self.assertIsNone(yaw.loop_range)
        with self.assertRaises(TypeError):
            yaw.set_loop(smdl.PoseLoopMode.RANGE)
        with self.assertRaises(TypeError):
            yaw.set_loop(smdl.PoseLoopMode.WRAP, 10)
        with self.assertRaises(ValueError):
            yaw.set_loop(7)


@unittest.skipIf(smdl is None, SKIP)
class IkChainAndWeightListTests(unittest.TestCase):
    def test_ik_chain_settings(self):
        model, root, arm, hand = skeleton_model()
        leg = model.add_ik_chain("leg", "hand")
        self.assertEqual((leg.name, leg.index, leg.end_bone), ("leg", 0, hand))
        self.assertIsNone(leg.height)
        leg.height = 20
        leg.floor = 1
        leg.pad = 6
        leg.knee = (1, 0, 0)
        leg.center = (0, 0, 5)
        self.assertEqual((leg.height, leg.floor, leg.pad), (20.0, 1.0, 6.0))
        self.assertEqual((leg.knee, leg.center), ((1.0, 0.0, 0.0), (0.0, 0.0, 5.0)))
        self.assertIsNone(leg.axis)
        leg.set_axis(smdl.Motion.Z, -1)
        self.assertEqual(leg.axis, (smdl.Motion.Z, -1.0))
        self.assertEqual(model.ik_chains, (leg,))
        self.assertEqual(model.find_ik_chain("LEG"), leg)
        with self.assertRaises(smdl.NotFoundError):
            model.find_ik_chain("arm")
        with self.assertRaises(TypeError):
            leg.height = "tall"

    def test_autoplay_locks_accept_chains_names_and_ids(self):
        model, root, arm, hand = skeleton_model()
        leg = model.add_ik_chain("leg", hand)
        self.assertEqual(model.add_ik_autoplay_lock(leg, 1.0, 0.5), 0)
        self.assertEqual(model.add_ik_autoplay_lock("leg", 1.0, 0.5), 1)
        self.assertEqual(model.add_ik_autoplay_lock(0, 1.0, 0.5), 2)
        with self.assertRaises(smdl.NotFoundError):
            model.add_ik_autoplay_lock("missing", 1.0, 0.5)
        with self.assertRaises(smdl.StudioMdlError):
            model.add_ik_autoplay_lock(5, 1.0, 0.5)
        with self.assertRaisesRegex(ValueError, "another model"):
            model.add_ik_autoplay_lock(foreign_chain(), 1.0, 0.5)
        with self.assertRaises(TypeError):
            model.add_ik_autoplay_lock(1.5, 1.0, 0.5)

    def test_weight_lists_include_the_default_list(self):
        model, root, arm, hand = skeleton_model()
        default = model.default_weight_list
        self.assertEqual(default.index, 0)
        self.assertIsNone(default.name)
        upper = model.add_weight_list("upper")
        self.assertEqual((upper.name, upper.index), ("upper", 1))
        self.assertEqual(model.weight_lists, (default, upper))
        self.assertEqual(model.find_weight_list("upper"), upper)
        upper.set_bone("arm", 1.0)
        upper.set_bone(hand, 0.5, position_weight=0.25)
        upper.set_bone(arm, 0.75)
        with self.assertRaises(smdl.NotFoundError):
            upper.set_bone("missing", 1.0)
        with self.assertRaises(smdl.DuplicateNameError):
            model.add_weight_list("UPPER")


def foreign_chain():
    other = smdl.Model("b")
    return other.add_ik_chain("leg", other.add_bone("root"))


@unittest.skipIf(smdl is None, SKIP)
class AnimationTests(unittest.TestCase):
    def test_animation_basics(self):
        model, root, arm, hand = skeleton_model()
        walk = model.create_animation("walk", 30, 4)
        self.assertEqual((walk.name, walk.fps, walk.frame_count, walk.command_count), ("walk", 30.0, 4, 0))
        walk.fps = 24
        self.assertEqual(walk.fps, 24.0)
        self.assertEqual(model.find_animation("WALK"), walk)
        run = model.create_animation("run", 30, 2)
        self.assertEqual(model.animations, (walk, run))
        self.assertEqual(repr(walk), "<Animation 'walk'>")
        with self.assertRaises(smdl.DuplicateNameError):
            model.create_animation("walk", 30, 4)
        with self.assertRaises(smdl.NotFoundError):
            model.find_animation("missing")
        with self.assertRaises(smdl.StudioMdlError):
            model.create_animation("zero", 30, 0)
        with self.assertRaises(smdl.StudioMdlError):
            model.create_animation("slow", 0, 3)

    def test_options_read_back_the_assigned_value(self):
        model, *_ = skeleton_model()
        walk = model.create_animation("walk", 30, 4)
        self.assertIsNone(walk.loop)
        walk.loop = True
        walk.scale = 2
        walk.origin = (1, 2, 3)
        walk.block_name = "idle"
        walk.motion_extraction = smdl.Motion.X | smdl.Motion.LY
        self.assertIs(walk.loop, True)
        self.assertEqual((walk.scale, walk.origin, walk.block_name), (2.0, (1.0, 2.0, 3.0), "idle"))
        self.assertEqual(walk.motion_extraction, smdl.Motion.X | smdl.Motion.LY)
        self.assertIsNone(model.create_animation("other", 30, 4).loop)
        walk.set_angles(0, 90, 0)
        walk.set_frame_range(1, 2)
        with self.assertRaises(TypeError):
            walk.loop = None
        with self.assertRaises(ValueError):
            walk.origin = (1, 2)

    def test_bone_frames_must_have_one_row_per_frame(self):
        model, root, arm, hand = skeleton_model()
        walk = model.create_animation("walk", 30, 4)
        positions, rotations = rest_frames(4)
        walk.set_bone_frames(root, positions, rotations)
        walk.set_bone_frames("arm", positions=positions)
        walk.set_bone_frames(hand, rotations=rotations)
        walk.set_bone_frames(root, positions, euler=[(0, 0, 0.1 * i) for i in range(4)])
        with self.assertRaisesRegex(ValueError, "positions must have 4 rows"):
            walk.set_bone_frames(root, positions[:3], rotations)
        with self.assertRaisesRegex(ValueError, "rotations must have 4 rows"):
            walk.set_bone_frames(root, positions, rotations[:2])
        with self.assertRaisesRegex(ValueError, "euler must have 4 rows"):
            walk.set_bone_frames(root, euler=[(0, 0, 0)] * 5)
        with self.assertRaises(ValueError):
            walk.set_bone_frames(root, [(0, 0)] * 4)
        with self.assertRaises(ValueError):
            walk.set_bone_frames(root, rotations=[(0, 0, 0)] * 4)
        with self.assertRaisesRegex(TypeError, "not both"):
            walk.set_bone_frames(root, rotations=rotations, euler=[(0, 0, 0)] * 4)
        with self.assertRaises(smdl.NotFoundError):
            walk.set_bone_frames("missing", positions)

    def test_bone_frames_take_float32_buffers(self):
        model, root, arm, hand = skeleton_model()
        walk = model.create_animation("walk", 30, 3)
        flat = array.array("f", [0, 0, 0, 0, 0, 1, 0, 0, 2])
        walk.set_bone_frames(root, flat)
        with self.assertRaises(ValueError):
            walk.set_bone_frames(root, array.array("f", [0, 0, 0, 0]))
        if numpy is not None:
            walk.set_bone_frames(root, numpy.zeros((3, 3), dtype=numpy.float32),
                                 numpy.tile(numpy.float32([0, 0, 0, 1]), (3, 1)))
            walk.set_bone_frames(root, numpy.zeros((3, 3)))  # float64 is converted
            with self.assertRaises(ValueError):
                walk.set_bone_frames(root, numpy.zeros((2, 3), dtype=numpy.float32))
            with self.assertRaises(ValueError):
                walk.set_bone_frames(root, numpy.zeros((3, 4), dtype=numpy.float32))

    def test_rest_pose_takes_a_quaternion_or_euler(self):
        model, root, arm, hand = skeleton_model()
        walk = model.create_animation("walk", 30, 2)
        walk.set_rest_pose(root, (0, 0, 5))
        walk.set_rest_pose("arm", rotation=(0, 0, 0, 1))
        walk.set_rest_pose(hand, euler=(0, 0, 1))
        with self.assertRaisesRegex(TypeError, "not both"):
            walk.set_rest_pose(root, rotation=(0, 0, 0, 1), euler=(0, 0, 0))

    def test_commands_are_counted_and_reordered(self):
        model, root, arm, hand = skeleton_model()
        model.add_ik_chain("leg", hand)
        weights = model.add_weight_list("upper")
        base = model.create_animation("base", 30, 4)
        walk = model.create_animation("walk", 30, 4)
        commands = [
            lambda: walk.add_fixup_loop(0, 3),
            lambda: walk.add_weight_list(weights),
            lambda: walk.add_subtract(base, 0),
            lambda: walk.add_subtract("base", 1, post=False),
            lambda: walk.add_align(base, smdl.Motion.X | smdl.Motion.Y, 0, 3),
            lambda: walk.add_align_to(base),
            lambda: walk.add_align_bone(arm, base, smdl.Motion.X, 0, 3),
            lambda: walk.add_align_bone_to("arm", base),
            lambda: walk.add_match(base),
            lambda: walk.add_match_blend(base, 0, 3, 1, 2),
            lambda: walk.add_world_space_blend(base),
            lambda: walk.add_world_space_blend_loop(base, 1),
            lambda: walk.add_rotate_to(90),
            lambda: walk.add_walk_frame(3, smdl.Motion.X),
            lambda: walk.add_walk_align(3, base, smdl.Motion.X, 0, 3),
            lambda: walk.add_walk_align_to(3, base, smdl.Motion.X),
            lambda: walk.add_derivative(1.5),
            lambda: walk.add_no_animation(),
            lambda: walk.add_no_animation(keep_duration=True),
            lambda: walk.add_linear_delta(),
            lambda: walk.add_spline_delta(),
            lambda: walk.add_compress(2),
            lambda: walk.add_num_frames(4),
            lambda: walk.add_counter_rotate(arm),
            lambda: walk.add_counter_rotate(arm, angles=(0, 90, 0)),
            lambda: walk.add_local_hierarchy(hand, arm),
            lambda: walk.add_local_hierarchy(hand, None, range=(0, 1, 2, None)),
            lambda: walk.add_force_bone_pos_rot(hand, position=(1, 2, 3)),
            lambda: walk.add_force_bone_pos_rot(hand, angles=(0, 90, 0), local=True),
            lambda: walk.add_bone_driver(hand, 2, 5.0),
            lambda: walk.add_bone_driver(hand, 1, 5.0, range=(0, 1, 2, 3)),
            lambda: walk.add_reverse(),
            lambda: walk.add_append_animation(base),
        ]
        for count, command in enumerate(commands, 1):
            command()
            self.assertEqual(walk.command_count, min(count, 64), count)
        self.assertEqual(walk.command_count, len(commands))
        walk.move_command(len(commands) - 1, 0)
        with self.assertRaises(smdl.StudioMdlError):
            walk.move_command(0, 100)

    def test_command_arguments_are_validated(self):
        model, root, arm, hand = skeleton_model()
        base = model.create_animation("base", 30, 4)
        walk = model.create_animation("walk", 30, 4)
        with self.assertRaises(smdl.StudioMdlError):
            walk.add_fixup_loop(0, 10)
        with self.assertRaises(smdl.NotFoundError):
            walk.add_match("missing")
        with self.assertRaisesRegex(ValueError, "another model"):
            walk.add_match(smdl.Model("b").create_animation("base", 30, 4))
        with self.assertRaises(TypeError):
            walk.add_match(5)
        with self.assertRaises(ValueError):
            walk.add_local_hierarchy(hand, range=(0, 1, 2))
        with self.assertRaises(TypeError):
            walk.add_local_hierarchy(hand, range=5)
        with self.assertRaises(TypeError):
            walk.add_subtract(base)
        with self.assertRaises(TypeError):
            walk.add_align(base, smdl.Motion.X, 0, 3, 7)
        self.assertEqual(walk.command_count, 0)


@unittest.skipIf(smdl is None, SKIP)
class IkRuleTests(unittest.TestCase):
    def test_ik_rules_are_configured_after_adding_them(self):
        model, root, arm, hand = skeleton_model()
        eye = model.add_attachment("eye", root)
        leg = model.add_ik_chain("leg", hand)
        walk = model.create_animation("walk", 30, 20)
        rule = walk.add_ik_rule(leg, smdl.IkRuleType.FOOTSTEP)
        self.assertEqual(walk.command_count, 1)
        self.assertIs(rule.type, smdl.IkRuleType.FOOTSTEP)
        self.assertEqual(rule.chain, leg)
        self.assertIsNone(rule.height)
        rule.height = 12
        rule.target = 1
        rule.floor = 2
        rule.pad = 8
        rule.radius = 3
        rule.contact = 4
        rule.use_sequence = True
        rule.use_source = False
        rule.fake_origin = (1, 2, 3)
        rule.set_range(0, 5, 10, None)
        self.assertEqual((rule.height, rule.target, rule.floor, rule.pad, rule.radius, rule.contact),
                         (12.0, 1, 2.0, 8.0, 3.0, 4))
        self.assertEqual((rule.use_sequence, rule.use_source, rule.fake_origin), (True, False, (1.0, 2.0, 3.0)))
        self.assertEqual(rule.range, (0, 5, 10, -1))
        rule.set_fake_rotation(angles=(0, 90, 0))
        rule.set_fake_rotation(euler=(0, 0, 1))
        rule.set_fake_rotation(rotation=(0, 0, 0, 1))
        with self.assertRaises(TypeError):
            rule.set_fake_rotation()
        with self.assertRaises(TypeError):
            rule.set_fake_rotation(angles=(0, 0, 0), euler=(0, 0, 0))

        touch = walk.add_ik_fixup("leg", smdl.IkRuleType.TOUCH)
        self.assertNotEqual(touch, rule)
        self.assertIsNone(touch.bone)
        touch.bone = "hand"
        self.assertEqual(touch.bone, hand)
        attachment = walk.add_ik_rule(0, smdl.IkRuleType.ATTACHMENT)
        attachment.attachment = eye
        self.assertEqual(attachment.attachment, eye)
        attachment.attachment = "eye"
        self.assertEqual(attachment.attachment, eye)
        steps = walk.add_ik_rule(leg, smdl.IkRuleType.AUTOSTEPS)
        steps.autosteps_end = 15
        self.assertEqual(steps.autosteps_end, 15)
        self.assertEqual(walk.command_count, 4)

    def test_errors(self):
        model, root, arm, hand = skeleton_model()
        leg = model.add_ik_chain("leg", hand)
        walk = model.create_animation("walk", 30, 20)
        with self.assertRaises(smdl.NotFoundError):
            walk.add_ik_rule("missing", smdl.IkRuleType.TOUCH)
        with self.assertRaises(TypeError):
            walk.add_ik_rule(leg)
        rule = walk.add_ik_rule(leg, smdl.IkRuleType.TOUCH)
        with self.assertRaises(TypeError):
            rule.bone = None
        with self.assertRaises(smdl.NotFoundError):
            rule.attachment = "missing"
        with self.assertRaises(TypeError):
            rule.height = "tall"


@unittest.skipIf(smdl is None, SKIP)
class SequenceTests(unittest.TestCase):
    def test_sequences_are_found_by_name(self):
        model, *_ = skeleton_model()
        idle = model.create_sequence("idle")
        walk = model.create_sequence("walk")
        self.assertEqual(idle.name, "idle")
        self.assertEqual(model.sequences, (idle, walk))
        self.assertEqual(model.find_sequence("WALK"), walk)
        self.assertEqual(repr(idle), "<Sequence 'idle'>")
        with self.assertRaises(smdl.DuplicateNameError):
            model.create_sequence("idle")
        with self.assertRaises(smdl.NotFoundError):
            model.find_sequence("missing")
        # Animations and sequences share their names
        model.create_animation("anim", 30, 2)
        with self.assertRaises(smdl.DuplicateNameError):
            model.create_sequence("anim")

    def test_settings_read_back_the_assigned_value(self):
        model, root, arm, hand = skeleton_model()
        yaw = model.add_pose_parameter("yaw", -1, 1)
        first = model.create_animation("a", 30, 2)
        second = model.create_animation("b", 30, 2)
        sequence = model.create_sequence("idle")
        self.assertIsNone(sequence.fade_in)
        sequence.fade_in = 0.25
        sequence.fade_out = 0.75
        sequence.exit_phase = 0.5
        sequence.node = "stand"
        sequence.blend_width = 2
        sequence.blend_reference = first
        sequence.blend_compare = "b"
        sequence.pose_cycle = "yaw"
        sequence.root_driver = hand
        self.assertEqual((sequence.fade_in, sequence.fade_out, sequence.exit_phase), (0.25, 0.75, 0.5))
        self.assertEqual((sequence.node, sequence.blend_width), ("stand", 2))
        self.assertEqual((sequence.blend_reference, sequence.blend_compare), (first, second))
        self.assertIsNone(sequence.blend_center)
        self.assertEqual((sequence.pose_cycle, sequence.root_driver), (yaw, hand))
        self.assertIsNone(model.create_sequence("other").fade_in)
        with self.assertRaises(TypeError):
            sequence.fade_in = "slow"
        with self.assertRaises(smdl.NotFoundError):
            sequence.blend_center = "missing"
        with self.assertRaises(TypeError):
            sequence.blend_center = None

    def test_builder_calls(self):
        model, root, arm, hand = skeleton_model()
        yaw = model.add_pose_parameter("yaw", -1, 1)
        eye = model.add_attachment("eye", root)
        leg = model.add_ik_chain("leg", hand)
        first = model.create_animation("a", 30, 10)
        second = model.create_animation("b", 30, 10)
        model.create_animation("c", 30, 10)
        layer = model.create_sequence("layer")
        sequence = model.create_sequence("blend")
        sequence.add_animation(first)
        sequence.add_animations([second, "c"])
        sequence.add_blend(yaw, -1, 1)
        sequence.add_calc_blend("yaw", eye, smdl.Motion.X)
        sequence.set_activity("ACT_IDLE", 2)
        sequence.add_activity_modifier("crouch")
        sequence.add_event("AE_FOOTSTEP", 3)
        sequence.add_event("AE_FOOTSTEP", 3, "left")
        sequence.add_event("AE_FOOTSTEP", options="right", cycle=0.5)
        sequence.add_anim_tag("tag", 0.25)
        sequence.set_flags(smdl.SequenceFlag.SNAP | smdl.SequenceFlag.AUTOPLAY)
        sequence.set_flags(smdl.SequenceFlag.SNAP, False)
        sequence.set_transition("a", "b")
        sequence.set_transition("b", "a", reverse=True)
        sequence.add_layer(layer)
        sequence.add_layer("layer", local=True)
        sequence.add_blend_layer(layer, 0, 2, 7, 9)
        sequence.add_blend_layer("layer", 0, 2, 7, 9, smdl.LayerFlag.XFADE | smdl.LayerFlag.NO_BLEND)
        sequence.add_blend_layer(layer, 0, 2, 7, 9, smdl.LayerFlag.POSE, yaw)
        sequence.add_ik_lock(leg, 1.0, 0.5)
        sequence.add_key_values("mykey myvalue")

    def test_argument_errors(self):
        model, root, arm, hand = skeleton_model()
        yaw = model.add_pose_parameter("yaw", -1, 1)
        model.create_animation("a", 30, 10)
        layer = model.create_sequence("layer")
        sequence = model.create_sequence("idle")
        with self.assertRaises(smdl.NotFoundError):
            sequence.add_animation("missing")
        with self.assertRaisesRegex(ValueError, "another model"):
            sequence.add_animation(smdl.Model("b").create_animation("a", 30, 10))
        with self.assertRaises(TypeError):
            sequence.add_animation(layer)
        with self.assertRaises(TypeError):
            sequence.add_animations("a")
        with self.assertRaises(TypeError):
            sequence.add_animations(5)
        with self.assertRaises(TypeError):
            sequence.add_event("AE_FOOTSTEP")
        with self.assertRaises(TypeError):
            sequence.add_event("AE_FOOTSTEP", 3, cycle=0.5)
        with self.assertRaisesRegex(TypeError, "pose_parameter"):
            sequence.add_blend_layer(layer, 0, 1, 2, 3, smdl.LayerFlag.POSE)
        with self.assertRaisesRegex(TypeError, "pose_parameter"):
            sequence.add_blend_layer(layer, 0, 1, 2, 3, 0, yaw)
        with self.assertRaises(smdl.NotFoundError):
            sequence.add_layer("missing")
        with self.assertRaises(smdl.NotFoundError):
            sequence.add_blend("missing", 0, 1)
        with self.assertRaises(TypeError):
            sequence.set_flags("snap")
        with self.assertRaises(smdl.StudioMdlError):
            sequence.set_flags(0x80000000)


@unittest.skipIf(smdl is None, SKIP)
class FlagEnumTests(unittest.TestCase):
    def test_flag_words_combine(self):
        self.assertEqual(smdl.Motion.X | smdl.Motion.Y, 3)
        self.assertIsInstance(smdl.Motion.X | smdl.Motion.LINEAR, smdl.Motion)
        self.assertEqual(smdl.SequenceFlag.SNAP | smdl.SequenceFlag.HIDDEN, 0x101)
        self.assertEqual(smdl.LayerFlag.XFADE | smdl.LayerFlag.POSE, 0x11)
        self.assertEqual(smdl.IkRuleType.FOOTSTEP, 2)
        self.assertEqual(smdl.PoseLoopMode.RANGE, 2)


@unittest.skipIf(smdl is None, SKIP)
class CompileTests(unittest.TestCase):
    def model_with_idle(self, name="props/anim", root_z=0.0):
        model = smdl.Model(name)
        root = model.add_bone("root")
        add_triangle_mesh(model)
        animation = model.create_animation("idle_anim", 30, 5)
        animation.set_bone_frames(root, *rest_frames(5, root_z))
        animation.loop = True
        sequence = model.create_sequence("idle")
        sequence.add_animation(animation)
        return model, root, animation, sequence

    def test_model_with_bones_animation_and_sequence_compiles(self):
        model, *_ = self.model_with_idle()
        result = model.compile()
        mdl = result.files["models/props/anim.mdl"]
        self.assertEqual(mdl[:4], b"IDST")
        self.assertIn(b"idle", mdl)

    def test_frame_data_changes_the_output(self):
        flat = self.model_with_idle(root_z=0.0)[0].compile().files["models/props/anim.mdl"]
        raised = self.model_with_idle(root_z=7.0)[0].compile().files["models/props/anim.mdl"]
        self.assertNotEqual(flat, raised)

    def test_rig_activity_events_and_flags_are_compiled(self):
        model, root, animation, sequence = self.model_with_idle()
        model.add_attachment("muzzle_flash", root, (1, 0, 5)).rigid = True
        default = model.add_hitbox_set("default")
        default.add_hitbox(0, root, (-2, -2, 0), (2, 2, 10), name="torso_box")
        yaw = model.add_pose_parameter("aim_yaw", -45, 45)
        yaw.set_loop(smdl.PoseLoopMode.NONE)
        sequence.set_activity("ACT_IDLE", 1)
        sequence.add_event("AE_CL_PLAYSOUND", 2, "weapons/click.wav")
        sequence.set_flags(smdl.SequenceFlag.AUTOPLAY)
        model.add_allowed_activity_name("ACT_IDLE")
        mdl = model.compile().files["models/props/anim.mdl"]
        for text in (b"muzzle_flash", b"ACT_IDLE", b"weapons/click.wav", b"aim_yaw", b"torso_box"):
            self.assertIn(text, mdl)

    def test_blend_sequence_with_a_pose_parameter_compiles(self):
        model, root, animation, sequence = self.model_with_idle()
        yaw = model.add_pose_parameter("aim_yaw", -45, 45)
        left = model.create_animation("left_anim", 30, 5)
        left.set_bone_frames(root, *rest_frames(5, 3.0))
        blend = model.create_sequence("aim")
        blend.add_animations([animation, left])
        blend.add_blend(yaw, -45, 45)
        mdl = model.compile().files["models/props/anim.mdl"]
        self.assertIn(b"aim_yaw", mdl)

    def test_sequence_without_animation_fails_naming_the_sequence(self):
        model, root, animation, sequence = self.model_with_idle()
        model.create_sequence("empty")
        with self.assertRaises(smdl.CompileError) as caught:
            model.compile()
        self.assertIn("empty", str(caught.exception))

    def test_animation_command_referring_to_a_later_animation_fails(self):
        model, root, animation, sequence = self.model_with_idle()
        late = model.create_animation("late_anim", 30, 5)
        animation.add_match(late)
        with self.assertRaises(smdl.CompileError):
            model.compile()


if __name__ == "__main__":
    unittest.main()
