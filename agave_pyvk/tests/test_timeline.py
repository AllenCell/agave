"""Tests for the animation timeline (agave_pyvk.animation / channels / drivers).

Named test_timeline.py rather than test_animation.py to avoid a pytest basename
collision with the agave_pyvk/agave_pyvk/test_animation.py example script.
"""

import ast
import math
from pathlib import Path

import pytest

from agave_pyvk import channels
from agave_pyvk.drivers import (
    CameraOrbit,
    pivot_camera,
    ramp,
    rotate_about_axis,
    travel_clip_plane,
)
from agave_pyvk.animation import (
    ArityError,
    Keyframes,
    MissingInitialError,
    OverlapError,
    Segment,
    Timeline,
    UnknownChannelError,
    ease_in,
    ease_in_out,
    ease_out,
    linear,
)


class Recorder:
    """Stands in for AgaveRenderer, recording (method, args) for every call."""

    def __init__(self):
        self.calls = []

    def __getattr__(self, name):
        def call(*args):
            self.calls.append((name, args))

        return call


def agave_renderer_methods():
    """Public AgaveRenderer method names, parsed without importing _native."""
    path = Path(__file__).resolve().parents[1] / "agave_pyvk" / "agave.py"
    tree = ast.parse(path.read_text(encoding="utf-8"))
    cls = next(
        node
        for node in tree.body
        if isinstance(node, ast.ClassDef) and node.name == "AgaveRenderer"
    )
    return {
        node.name
        for node in cls.body
        if isinstance(node, ast.FunctionDef) and not node.name.startswith("_")
    }


class TestEasing:
    @pytest.mark.parametrize("ease", [linear, ease_in, ease_out, ease_in_out])
    def test_maps_unit_interval_endpoints_to_themselves(self, ease):
        assert ease(0.0) == 0.0
        assert ease(1.0) == 1.0

    def test_ease_in_out_is_smoothstep(self):
        assert ease_in_out(0.5) == 0.5
        assert ease_in_out(0.2) == pytest.approx(0.104)


class TestKeyframes:
    def test_spans_from_first_to_last_point(self):
        kf = Keyframes("x", [(3, (0.0,)), (7, (1.0,)), (12, (2.0,))])
        assert (kf.start, kf.end) == (3, 12)

    def test_writes_only_its_own_channel(self):
        kf = Keyframes("x", [(0, (0.0,)), (10, (1.0,))])
        assert kf.writes() == ("x",)

    def test_returns_exact_values_at_the_endpoint_frames(self):
        kf = Keyframes("x", [(0, (0.0,)), (10, (5.0,))])
        assert kf.evaluate(0) == {"x": (0.0,)}
        assert kf.evaluate(10) == {"x": (5.0,)}

    def test_interpolates_linearly_between_two_points(self):
        kf = Keyframes("x", [(0, (0.0,)), (10, (5.0,))])
        assert kf.evaluate(5) == {"x": (2.5,)}

    def test_interpolates_every_component_of_a_vector_channel(self):
        kf = Keyframes("v", [(0, (0.0, 10.0)), (2, (2.0, 20.0))])
        assert kf.evaluate(1) == {"v": (1.0, 15.0)}

    def test_uses_the_bracketing_pair_on_a_multi_point_track(self):
        kf = Keyframes("x", [(0, (0.0,)), (10, (10.0,)), (20, (0.0,))])
        assert kf.evaluate(15) == {"x": (5.0,)}

    def test_applies_easing_between_each_pair(self):
        kf = Keyframes("x", [(0, (0.0,)), (10, (1.0,))], ease=ease_in_out)
        assert kf.evaluate(2)["x"][0] == pytest.approx(0.104)

    def test_promotes_bare_scalars_to_one_tuples(self):
        kf = Keyframes("x", [(0, 0.0), (10, 5.0)])
        assert kf.evaluate(5) == {"x": (2.5,)}

    def test_rejects_fewer_than_two_points(self):
        with pytest.raises(ValueError, match="at least two"):
            Keyframes("x", [(0, (0.0,))])

    def test_rejects_non_increasing_frames(self):
        with pytest.raises(ValueError, match="increasing"):
            Keyframes("x", [(0, (0.0,)), (0, (1.0,))])

    def test_rejects_points_of_differing_arity(self):
        with pytest.raises(ValueError, match="arity"):
            Keyframes("x", [(0, (0.0,)), (10, (1.0, 2.0))])


class TestChannelNamespace:
    def test_resolves_a_plain_channel_to_its_arity_and_call(self):
        spec = channels.resolve("camera.eye")
        assert spec.arity == 3
        r = Recorder()
        spec.setter(r, (1.0, 2.0, 3.0))
        assert r.calls == [("eye", (1.0, 2.0, 3.0))]

    def test_resolves_the_clip_plane_as_four_floats(self):
        spec = channels.resolve("clip.plane")
        assert spec.arity == 4
        r = Recorder()
        spec.setter(r, (0.0, 0.0, 1.0, 0.25))
        assert r.calls == [("set_clip_plane", (0.0, 0.0, 1.0, 0.25))]

    def test_resolves_an_indexed_channel_and_binds_the_index(self):
        spec = channels.resolve("mat[2].opacity")
        assert spec.arity == 1
        r = Recorder()
        spec.setter(r, (0.5,))
        assert r.calls == [("mat_opacity", (2, 0.5))]

    def test_indexed_channels_carry_their_family_arity(self):
        spec = channels.resolve("mat[0].diffuse")
        assert spec.arity == 4
        r = Recorder()
        spec.setter(r, (0.1, 0.2, 0.3, 1.0))
        assert r.calls == [("mat_diffuse", (0, 0.1, 0.2, 0.3, 1.0))]

    def test_alias_resolves_to_the_same_call_as_its_target(self):
        channels.alias("membrane.opacity", "mat[0].opacity")
        try:
            r = Recorder()
            channels.resolve("membrane.opacity").setter(r, (2.13,))
            assert r.calls == [("mat_opacity", (0, 2.13))]
        finally:
            channels.ALIASES.pop("membrane.opacity", None)

    def test_alias_to_an_unknown_target_is_rejected_at_definition(self):
        with pytest.raises(UnknownChannelError, match="nope"):
            channels.alias("shorthand", "nope")

    def test_unknown_channel_name_is_rejected(self):
        with pytest.raises(UnknownChannelError, match="camera.nose"):
            channels.resolve("camera.nose")

    def test_unknown_indexed_family_is_rejected(self):
        with pytest.raises(UnknownChannelError, match=r"bogus\[0\].thing"):
            channels.resolve("bogus[0].thing")

    def test_every_channel_calls_a_real_agave_renderer_method(self):
        """Guards the namespace against drifting from the AgaveRenderer API."""
        available = agave_renderer_methods()
        for name in channels.names():
            spec = channels.resolve(name)
            r = Recorder()
            spec.setter(r, tuple(0.0 for _ in range(spec.arity)))
            called, _ = r.calls[0]
            assert called in available, f"{name} calls missing method {called}()"

    def test_names_covers_plain_and_indexed_channels(self):
        listed = set(channels.names())
        assert "camera.eye" in listed
        assert "mat[0].opacity" in listed


def kf_segment(channel, v0, v1, *, secs=None, frames=None):
    """A minimal Segment for exercising the Timeline without drivers.py."""
    return Segment(secs, frames, lambda s, e: Keyframes(channel, [(s, v0), (e, v1)]))


class TestTimelinePlacement:
    def test_then_places_a_segment_from_the_cursor(self):
        tl = Timeline(fps=24, initial={"camera.exposure": (0.0,)})
        tl.then(kf_segment("camera.exposure", 0.0, 1.0, secs=2))
        assert tl.spans() == [("camera.exposure", 0, 48)]

    def test_then_places_several_segments_sequentially(self):
        tl = Timeline(fps=24, initial={"camera.exposure": (0.0,)})
        tl.then(
            kf_segment("camera.exposure", 0.0, 1.0, secs=2),
            kf_segment("camera.exposure", 1.0, 0.0, secs=2),
        )
        assert tl.spans() == [
            ("camera.exposure", 0, 48),
            ("camera.exposure", 48, 96),
        ]

    def test_together_places_from_the_same_cursor(self):
        tl = Timeline(
            fps=24, initial={"camera.exposure": (0.0,), "render.density": (0.0,)}
        )
        tl.then(kf_segment("camera.exposure", 0.0, 1.0, secs=1))
        tl.together(
            kf_segment("camera.exposure", 1.0, 0.0, secs=4),
            kf_segment("render.density", 0.0, 50.0, secs=2),
        )
        assert tl.spans() == [
            ("camera.exposure", 0, 24),
            ("camera.exposure", 24, 120),
            ("render.density", 24, 72),
        ]

    def test_together_advances_the_cursor_by_the_longest_segment(self):
        tl = Timeline(
            fps=24, initial={"camera.exposure": (0.0,), "render.density": (0.0,)}
        )
        tl.together(
            kf_segment("camera.exposure", 0.0, 1.0, secs=4),
            kf_segment("render.density", 0.0, 50.0, secs=2),
        )
        tl.then(kf_segment("camera.exposure", 1.0, 0.0, secs=1))
        assert ("camera.exposure", 96, 120) in tl.spans()

    def test_at_parks_the_cursor_in_seconds(self):
        tl = Timeline(fps=24, initial={"camera.exposure": (0.0,)})
        tl.at(secs=2.0).then(kf_segment("camera.exposure", 0.0, 1.0, secs=1))
        assert tl.spans() == [("camera.exposure", 48, 72)]

    def test_at_parks_the_cursor_in_frames(self):
        tl = Timeline(fps=24, initial={"camera.exposure": (0.0,)})
        tl.at(frames=48).then(kf_segment("camera.exposure", 0.0, 1.0, frames=12))
        assert tl.spans() == [("camera.exposure", 48, 60)]

    def test_last_frame_is_the_end_of_the_final_driver(self):
        tl = Timeline(fps=24, initial={"camera.exposure": (0.0,)})
        tl.then(kf_segment("camera.exposure", 0.0, 1.0, secs=2))
        assert tl.last_frame == 48


class TestTimelineTimeConversion:
    def test_cumulative_conversion_does_not_drift(self):
        """Ten 0.1s segments at 24fps must end on frame 24, not 20."""
        tl = Timeline(fps=24, initial={"camera.exposure": (0.0,)})
        for _ in range(10):
            tl.then(kf_segment("camera.exposure", 0.0, 1.0, secs=0.1))
        assert tl.last_frame == 24

    def test_frame_specified_segments_span_exactly_that_many_frames(self):
        tl = Timeline(fps=24, initial={"camera.exposure": (0.0,)})
        tl.then(kf_segment("camera.exposure", 0.0, 1.0, secs=0.1))
        tl.then(kf_segment("camera.exposure", 1.0, 0.0, frames=10))
        (_, s0, e0), (_, s1, e1) = tl.spans()
        assert e1 - s1 == 10
        assert s1 == e0

    def test_duration_requires_exactly_one_of_secs_or_frames(self):
        tl = Timeline(fps=24, initial={"camera.exposure": (0.0,)})
        with pytest.raises(ValueError, match="exactly one"):
            tl.then(kf_segment("camera.exposure", 0.0, 1.0, secs=1, frames=24))
        with pytest.raises(ValueError, match="exactly one"):
            tl.then(kf_segment("camera.exposure", 0.0, 1.0))

    def test_at_requires_exactly_one_of_secs_or_frames(self):
        tl = Timeline(fps=24, initial={"camera.exposure": (0.0,)})
        with pytest.raises(ValueError, match="exactly one"):
            tl.at(secs=1.0, frames=24)
        with pytest.raises(ValueError, match="exactly one"):
            tl.at()

    def test_rejects_a_non_positive_fps(self):
        with pytest.raises(ValueError, match="fps"):
            Timeline(fps=0, initial={})


class TestTimelineSampling:
    def test_holds_the_initial_value_before_the_first_driver(self):
        tl = Timeline(fps=24, initial={"camera.exposure": (0.75,)})
        tl.at(frames=10).then(kf_segment("camera.exposure", 0.0, 1.0, frames=10))
        assert tl.sample(0) == {"camera.exposure": (0.75,)}
        assert tl.sample(9) == {"camera.exposure": (0.75,)}

    def test_holds_the_final_value_after_the_last_driver(self):
        tl = Timeline(fps=24, initial={"camera.exposure": (0.0,)})
        tl.then(kf_segment("camera.exposure", 0.0, 1.0, frames=10))
        assert tl.sample(10) == {"camera.exposure": (1.0,)}
        assert tl.sample(50) == {"camera.exposure": (1.0,)}

    def test_adjacent_segments_agree_on_their_shared_boundary_frame(self):
        tl = Timeline(fps=24, initial={"camera.exposure": (0.0,)})
        tl.then(
            kf_segment("camera.exposure", 0.0, 45.0, frames=48),
            kf_segment("camera.exposure", 45.0, 0.0, frames=48),
        )
        assert tl.sample(48) == {"camera.exposure": (45.0,)}
        assert tl.sample(0) == {"camera.exposure": (0.0,)}
        assert tl.sample(96) == {"camera.exposure": (0.0,)}

    def test_abutting_segments_produce_no_duplicate_consecutive_frames(self):
        tl = Timeline(fps=24, initial={"camera.exposure": (0.0,)})
        tl.then(
            kf_segment("camera.exposure", 0.0, 45.0, frames=48),
            kf_segment("camera.exposure", 45.0, 0.0, frames=48),
        )
        values = [tl.sample(f)["camera.exposure"] for f in range(97)]
        assert all(a != b for a, b in zip(values, values[1:]))

    def test_samples_independent_channels_independently(self):
        tl = Timeline(
            fps=24, initial={"camera.exposure": (0.0,), "render.density": (5.0,)}
        )
        tl.then(kf_segment("camera.exposure", 0.0, 1.0, frames=10))
        assert tl.sample(5) == {
            "camera.exposure": (0.5,),
            "render.density": (5.0,),
        }


class TestTimelineValidation:
    def test_rejects_a_driver_on_an_unknown_channel(self):
        tl = Timeline(fps=24, initial={"camera.nose": (0.0,)})
        tl.then(kf_segment("camera.nose", 0.0, 1.0, frames=10))
        with pytest.raises(UnknownChannelError, match="camera.nose"):
            tl.sample(0)

    def test_rejects_a_value_of_the_wrong_arity_for_its_channel(self):
        tl = Timeline(fps=24, initial={"camera.eye": (0.0, 0.0, 0.0)})
        tl.then(kf_segment("camera.eye", 0.0, 1.0, frames=10))
        with pytest.raises(ArityError, match="camera.eye"):
            tl.sample(0)

    def test_rejects_an_initial_value_of_the_wrong_arity(self):
        tl = Timeline(fps=24, initial={"camera.eye": (0.0,)})
        tl.then(kf_segment("camera.eye", (0.0, 0.0, 0.0), (1.0, 1.0, 1.0), frames=10))
        with pytest.raises(ArityError, match="camera.eye"):
            tl.sample(0)

    def test_rejects_overlapping_drivers_on_one_channel(self):
        tl = Timeline(fps=24, initial={"camera.exposure": (0.0,)})
        tl.then(kf_segment("camera.exposure", 0.0, 1.0, frames=48))
        tl.at(frames=24).then(kf_segment("camera.exposure", 1.0, 0.0, frames=48))
        with pytest.raises(OverlapError, match="camera.exposure"):
            tl.sample(0)

    def test_allows_drivers_that_only_touch_at_a_boundary_frame(self):
        tl = Timeline(fps=24, initial={"camera.exposure": (0.0,)})
        tl.then(
            kf_segment("camera.exposure", 0.0, 1.0, frames=48),
            kf_segment("camera.exposure", 1.0, 0.0, frames=48),
        )
        assert tl.sample(48) == {"camera.exposure": (1.0,)}

    def test_rejects_a_driven_channel_with_no_initial_value(self):
        tl = Timeline(fps=24, initial={})
        tl.then(kf_segment("camera.exposure", 0.0, 1.0, frames=10))
        with pytest.raises(MissingInitialError, match="camera.exposure"):
            tl.sample(0)


class FakeRenderer(Recorder):
    """Recorder that also writes an RGBA frame, as agave_pyvk.redraw() does.

    Background pixels carry the composited RGB but zero alpha, which is exactly
    what renderlib's headless redraw produces.
    """

    BACKGROUND = (51, 77, 128)
    VOLUME = (200, 100, 50)

    def session(self, name):
        self.calls.append(("session", (name,)))
        self._path = name

    def redraw(self):
        from PIL import Image

        self.calls.append(("redraw", ()))
        image = Image.new("RGBA", (4, 2), self.BACKGROUND + (0,))
        image.putpixel((1, 1), self.VOLUME + (255,))
        image.save(self._path)

    def method_calls(self, name):
        return [args for called, args in self.calls if called == name]


class TestRender:
    def build(self):
        tl = Timeline(
            fps=24,
            initial={"camera.exposure": (0.75,), "camera.eye": (1.0, 2.0, 3.0)},
        )
        tl.then(kf_segment("camera.exposure", 0.0, 1.0, frames=4))
        return tl

    def test_renders_one_more_frame_than_the_span(self, tmp_path):
        r = FakeRenderer()
        self.build().render(r, tmp_path, "frame")
        assert len(r.method_calls("redraw")) == 5
        assert sorted(p.name for p in tmp_path.glob("*.png")) == [
            f"frame_{i:04d}.png" for i in range(5)
        ]

    def test_drop_last_renders_exactly_duration_times_fps_frames(self, tmp_path):
        r = FakeRenderer()
        tl = Timeline(fps=24, initial={"camera.exposure": (0.0,)})
        tl.then(kf_segment("camera.exposure", 0.0, 1.0, secs=2))
        tl.render(r, tmp_path, "frame", drop_last=True)
        assert len(r.method_calls("redraw")) == 48

    def test_emits_every_channel_on_the_first_frame(self, tmp_path):
        r = FakeRenderer()
        self.build().render(r, tmp_path, "frame")
        before_first_redraw = r.calls[: r.calls.index(("redraw", ()))]
        emitted = {called for called, _ in before_first_redraw}
        assert {"exposure", "eye"} <= emitted

    def test_does_not_re_emit_a_held_channel(self, tmp_path):
        r = FakeRenderer()
        self.build().render(r, tmp_path, "frame")
        assert r.method_calls("eye") == [(1.0, 2.0, 3.0)]

    def test_re_emits_a_channel_only_while_it_changes(self, tmp_path):
        r = FakeRenderer()
        self.build().render(r, tmp_path, "frame")
        assert r.method_calls("exposure") == [(0.0,), (0.25,), (0.5,), (0.75,), (1.0,)]

    def test_sets_channels_before_asking_for_the_frame(self, tmp_path):
        r = FakeRenderer()
        self.build().render(r, tmp_path, "frame")
        names = [called for called, _ in r.calls]
        assert names.index("exposure") < names.index("session") < names.index("redraw")

    def test_creates_the_output_directory(self, tmp_path):
        r = FakeRenderer()
        out = tmp_path / "nested" / "frames"
        self.build().render(r, out, "frame")
        assert out.is_dir()

    def test_flattens_alpha_so_frames_are_opaque(self, tmp_path):
        from PIL import Image

        r = FakeRenderer()
        self.build().render(r, tmp_path, "frame")
        image = Image.open(tmp_path / "frame_0000.png")
        assert image.mode == "RGB"
        assert image.getpixel((0, 0)) == FakeRenderer.BACKGROUND
        assert image.getpixel((1, 1)) == FakeRenderer.VOLUME

    def test_keeps_alpha_when_flattening_is_disabled(self, tmp_path):
        from PIL import Image

        r = FakeRenderer()
        self.build().render(r, tmp_path, "frame", flatten_alpha=False)
        image = Image.open(tmp_path / "frame_0000.png")
        assert image.mode == "RGBA"
        assert image.getpixel((0, 0))[3] == 0


class TestVectorMath:
    def test_rotates_a_vector_about_an_axis_by_the_right_hand_rule(self):
        rotated = rotate_about_axis((0.0, 0.0, 1.0), (0.0, 1.0, 0.0), 90.0)
        assert rotated == pytest.approx((1.0, 0.0, 0.0), abs=1e-12)

    def test_a_zero_degree_rotation_is_the_identity(self):
        v = (0.3, -0.4, 0.5)
        assert rotate_about_axis(v, (0.0, 1.0, 0.0), 0.0) == pytest.approx(v)


class TestCameraOrbit:
    EYE = (0.0, 0.0, 1.0)
    TARGET = (0.0, 0.0, 0.0)
    UP = (0.0, 1.0, 0.0)

    def orbit(self, deg0, deg1, start=0, end=10):
        return CameraOrbit(self.EYE, self.TARGET, self.UP, deg0, deg1, start, end)

    def test_writes_both_camera_channels(self):
        assert set(self.orbit(0, 45).writes()) == {"camera.eye", "camera.up"}

    def test_reproduces_the_reference_view_at_zero_degrees(self):
        values = self.orbit(0, 45).evaluate(0)
        assert values["camera.eye"] == pytest.approx(self.EYE)
        assert values["camera.up"] == pytest.approx(self.UP)

    def test_rotates_the_eye_about_the_vertical_view_axis(self):
        values = self.orbit(0, 90).evaluate(10)
        assert values["camera.eye"] == pytest.approx((1.0, 0.0, 0.0), abs=1e-12)

    def test_preserves_the_eye_to_target_distance(self):
        driver = self.orbit(0, 45)
        for frame in range(11):
            eye = driver.evaluate(frame)["camera.eye"]
            assert math.dist(eye, self.TARGET) == pytest.approx(1.0)

    def test_keeps_up_perpendicular_to_the_look_direction(self):
        driver = self.orbit(0, 45)
        for frame in range(11):
            values = driver.evaluate(frame)
            eye, up = values["camera.eye"], values["camera.up"]
            forward = [t - e for t, e in zip(self.TARGET, eye)]
            assert sum(a * b for a, b in zip(up, forward)) == pytest.approx(0.0)

    def test_returns_to_the_reference_view_after_a_round_trip(self):
        out = self.orbit(0, 45).evaluate(10)
        back = self.orbit(45, 0).evaluate(10)
        assert back["camera.eye"] == pytest.approx(self.EYE)
        assert out["camera.eye"] != pytest.approx(self.EYE)

    def test_handles_an_up_vector_that_is_not_perpendicular_to_the_view(self):
        """The vertical axis is up with the look-direction component removed."""
        skewed = CameraOrbit(self.EYE, self.TARGET, (0.0, 1.0, 0.5), 0, 90, 0, 10)
        eye = skewed.evaluate(10)["camera.eye"]
        assert eye == pytest.approx((1.0, 0.0, 0.0), abs=1e-12)


class TestSegmentHelpers:
    def test_ramp_interpolates_a_channel_between_two_values(self):
        tl = Timeline(fps=24, initial={"camera.exposure": (0.0,)})
        tl.then(ramp("camera.exposure", 0.0, 1.0, frames=10))
        assert tl.sample(5) == {"camera.exposure": (0.5,)}

    def test_ramp_carries_its_easing(self):
        tl = Timeline(fps=24, initial={"camera.exposure": (0.0,)})
        tl.then(ramp("camera.exposure", 0.0, 1.0, frames=10, ease=ease_in_out))
        assert tl.sample(2)["camera.exposure"][0] == pytest.approx(0.104)

    def test_ramp_handles_vector_channels(self):
        tl = Timeline(fps=24, initial={"camera.eye": (0.0, 0.0, 0.0)})
        tl.then(ramp("camera.eye", (0.0, 0.0, 0.0), (2.0, 4.0, 6.0), frames=10))
        assert tl.sample(5) == {"camera.eye": (1.0, 2.0, 3.0)}

    def test_travel_clip_plane_holds_the_normal_and_moves_the_distance(self):
        tl = Timeline(fps=24, initial={"clip.plane": (0.0, 0.0, 1.0, 1.0)})
        tl.then(travel_clip_plane((0.0, 0.0, 1.0), 1.0, 0.0, frames=10))
        assert tl.sample(5) == {"clip.plane": (0.0, 0.0, 1.0, 0.5)}
        assert tl.sample(10) == {"clip.plane": (0.0, 0.0, 1.0, 0.0)}

    def test_travel_clip_plane_normalizes_the_normal(self):
        tl = Timeline(fps=24, initial={"clip.plane": (0.0, 0.0, 1.0, 1.0)})
        tl.then(travel_clip_plane((0.0, 0.0, 2.0), 1.0, 1.0, frames=10))
        assert tl.sample(0) == {"clip.plane": (0.0, 0.0, 1.0, 1.0)}

    def test_pivot_camera_drives_both_camera_channels(self):
        tl = Timeline(
            fps=24,
            initial={"camera.eye": (0.0, 0.0, 1.0), "camera.up": (0.0, 1.0, 0.0)},
        )
        tl.then(
            pivot_camera(
                (0.0, 0.0, 1.0), (0.0, 0.0, 0.0), (0.0, 1.0, 0.0), 0, 90, frames=10
            )
        )
        assert tl.sample(0)["camera.eye"] == pytest.approx((0.0, 0.0, 1.0))
        assert tl.sample(10)["camera.eye"] == pytest.approx((1.0, 0.0, 0.0), abs=1e-12)

    def test_helpers_require_exactly_one_of_secs_or_frames(self):
        tl = Timeline(fps=24, initial={"camera.exposure": (0.0,)})
        with pytest.raises(ValueError, match="exactly one"):
            tl.then(ramp("camera.exposure", 0.0, 1.0, secs=1, frames=24))


class TestRenderProgress:
    def test_reports_each_frame_and_the_total(self, tmp_path):
        r = FakeRenderer()
        tl = Timeline(fps=24, initial={"camera.exposure": (0.0,)})
        tl.then(kf_segment("camera.exposure", 0.0, 1.0, frames=4))
        seen = []
        tl.render(r, tmp_path, "frame", progress=lambda f, n: seen.append((f, n)))
        assert seen == [(0, 5), (1, 5), (2, 5), (3, 5), (4, 5)]

    def test_reports_the_reduced_total_when_dropping_the_last_frame(self, tmp_path):
        r = FakeRenderer()
        tl = Timeline(fps=24, initial={"camera.exposure": (0.0,)})
        tl.then(kf_segment("camera.exposure", 0.0, 1.0, frames=4))
        seen = []
        tl.render(
            r,
            tmp_path,
            "frame",
            drop_last=True,
            progress=lambda f, n: seen.append((f, n)),
        )
        assert seen == [(0, 4), (1, 4), (2, 4), (3, 4)]


class TestExampleAnimation:
    """The shot declared by the example script, checked without rendering."""

    NORMAL = (0.0, 0.0, 1.0)
    D_START = 0.11644191741943359
    D_END = 0.058220958709716795

    def timeline(self):
        from agave_pyvk import test_animation

        return test_animation, test_animation.build_timeline(
            self.NORMAL, self.D_START, self.D_END
        )

    def test_runs_sixteen_seconds_at_twenty_four_fps(self):
        _, tl = self.timeline()
        assert tl.last_frame == 384

    def test_places_each_phase_on_the_expected_frames(self):
        _, tl = self.timeline()
        assert tl.spans() == [
            ("camera.eye", 0, 48),
            ("camera.up", 0, 48),
            ("camera.eye", 48, 96),
            ("camera.up", 48, 96),
            ("clip.plane", 96, 192),
            ("membrane.opacity", 192, 240),
            ("clip.plane", 240, 336),
            ("membrane.opacity", 336, 384),
        ]

    def test_opens_on_the_original_view_with_nothing_clipped(self):
        script, tl = self.timeline()
        state = tl.sample(0)
        assert state["camera.eye"] == pytest.approx(script.CAMERA_EYE)
        assert state["camera.up"] == pytest.approx(script.CAMERA_UP)
        assert state["clip.plane"] == pytest.approx((*self.NORMAL, self.D_START))
        assert state["membrane.opacity"] == (script.MEMBRANE_OPACITY,)

    def test_pivot_peaks_at_the_halfway_mark_and_returns(self):
        script, tl = self.timeline()
        assert tl.sample(48)["camera.eye"] != pytest.approx(script.CAMERA_EYE)
        assert tl.sample(96)["camera.eye"] == pytest.approx(script.CAMERA_EYE)

    def test_camera_is_still_once_the_clip_plane_starts_moving(self):
        script, tl = self.timeline()
        for frame in (96, 192, 240, 336, 384):
            assert tl.sample(frame)["camera.eye"] == pytest.approx(script.CAMERA_EYE)

    def test_clip_plane_travels_in_and_back(self):
        _, tl = self.timeline()
        assert tl.sample(96)["clip.plane"][3] == pytest.approx(self.D_START)
        assert tl.sample(192)["clip.plane"][3] == pytest.approx(self.D_END)
        assert tl.sample(240)["clip.plane"][3] == pytest.approx(self.D_END)
        assert tl.sample(336)["clip.plane"][3] == pytest.approx(self.D_START)

    def test_membrane_fades_out_then_back_in(self):
        script, tl = self.timeline()
        opacity = script.MEMBRANE_OPACITY
        assert tl.sample(192)["membrane.opacity"] == pytest.approx((opacity,))
        assert tl.sample(240)["membrane.opacity"] == pytest.approx((0.0,))
        assert tl.sample(336)["membrane.opacity"] == pytest.approx((0.0,))
        assert tl.sample(384)["membrane.opacity"] == pytest.approx((opacity,))

    def test_ends_on_the_state_it_started_from(self):
        _, tl = self.timeline()
        assert tl.sample(384) == pytest.approx(tl.sample(0))
