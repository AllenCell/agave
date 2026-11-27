"""Animation timeline for AGAVE scenes.

Three concepts, following the vocabulary of DCC animation systems:

``Channel``
    The timeline of values for one named parameter set, plus the single AGAVE
    call that applies it. ``camera.eye`` holds 3 floats applied with ``r.eye(*v)``.

``Driver``
    Occupies an inclusive frame span and produces values for one or more
    channels. A driver may be a higher-level construct: ``CameraOrbit`` takes one
    angle and writes both ``camera.eye`` and ``camera.up``.

``Timeline``
    Owns the drivers, resolves seconds to integer frames, samples every channel
    each frame, and emits only the channels whose value changed.

Derived math lives upstream in the driver, which keeps every channel a 1:1
mapping onto a single AGAVE call.
"""

from abc import ABC, abstractmethod
from dataclasses import dataclass
from pathlib import Path
from typing import Any, Callable, Dict, Sequence, Tuple, Union

Value = Tuple[float, ...]
Ease = Callable[[float], float]

Point = Tuple[int, Union[float, Sequence[float]]]


# --- errors -----------------------------------------------------------------


class AnimationError(Exception):
    """Base class for every timeline error."""


class UnknownChannelError(AnimationError):
    """A channel name is not in the registry."""


class ArityError(AnimationError):
    """A value has the wrong number of components for its channel."""


class MissingInitialError(AnimationError):
    """A driven channel has no declared initial value."""


# --- channel specs ----------------------------------------------------------


@dataclass(frozen=True)
class ChannelSpec:
    """How many floats a channel carries, and the AGAVE call that applies them."""

    arity: int
    setter: Callable[[Any, Value], None]


# --- easing -----------------------------------------------------------------
# Unitless: they map the unit interval onto itself, so they are independent of
# both fps and duration.


def linear(u: float) -> float:
    return u


def ease_in(u: float) -> float:
    return u * u


def ease_out(u: float) -> float:
    return 1.0 - (1.0 - u) * (1.0 - u)


def ease_in_out(u: float) -> float:
    """Smoothstep."""
    return u * u * (3.0 - 2.0 * u)


def as_value(v: Union[float, Sequence[float]]) -> Value:
    """Promote a bare scalar to a 1-tuple; leave sequences as float tuples."""
    if isinstance(v, (int, float)):
        return (float(v),)
    return tuple(float(x) for x in v)


def lerp(a: Value, b: Value, u: float) -> Value:
    return tuple(x + (y - x) * u for x, y in zip(a, b))


# --- drivers ----------------------------------------------------------------


class Driver(ABC):
    """Produces values for one or more channels over an inclusive frame span.

    A driver also reports a weight at each frame. Where several drivers cover
    the same channel at the same frame, the channel's value is their weighted
    mean, ``sum(w * v) / sum(w)``. The default weight is 1.0, so equally
    weighted drivers simply average; a weight that varies over time crossfades.
    """

    start: int
    end: int
    _weight: Union[float, Callable[[int], float]] = 1.0

    def weight(self, frame: int) -> float:
        """This driver's blending weight at ``frame``. Defaults to 1.0."""
        w = self._weight
        return float(w(frame)) if callable(w) else float(w)

    @abstractmethod
    def writes(self) -> Tuple[str, ...]:
        """Names of the channels this driver drives."""

    @abstractmethod
    def evaluate(self, frame: int) -> Dict[str, Value]:
        """Values for this driver's channels at ``frame``, which lies in span."""

    def progress(self, frame: int) -> float:
        """Un-eased position within the span, clamped to [0, 1]."""
        if self.end == self.start:
            return 0.0
        u = (frame - self.start) / (self.end - self.start)
        return min(1.0, max(0.0, u))


class Keyframes(Driver):
    """One channel, interpolated through a list of ``(frame, value)`` points.

    The same easing function is applied independently between each consecutive
    pair of points.
    """

    def __init__(
        self,
        channel: str,
        points: Sequence[Point],
        ease: Ease = linear,
        weight: Union[float, Callable[[int], float]] = 1.0,
    ):
        if len(points) < 2:
            raise ValueError(
                f"channel {channel!r}: a Keyframes driver needs at least two points"
            )

        self.channel = channel
        self.ease = ease
        self._weight = weight
        self.points = [(int(f), as_value(v)) for f, v in points]

        frames = [f for f, _ in self.points]
        if any(b <= a for a, b in zip(frames, frames[1:])):
            raise ValueError(
                f"channel {channel!r}: keyframe frames must be strictly "
                f"increasing, got {frames}"
            )

        arities = {len(v) for _, v in self.points}
        if len(arities) > 1:
            raise ValueError(
                f"channel {channel!r}: every keyframe must have the same arity, "
                f"got {sorted(arities)}"
            )

        self.arity = arities.pop()
        self.start = frames[0]
        self.end = frames[-1]

    def writes(self) -> Tuple[str, ...]:
        return (self.channel,)

    def evaluate(self, frame: int) -> Dict[str, Value]:
        return {self.channel: self._sample(frame)}

    def _sample(self, frame: int) -> Value:
        if frame <= self.start:
            return self.points[0][1]
        if frame >= self.end:
            return self.points[-1][1]

        for (f0, v0), (f1, v1) in zip(self.points, self.points[1:]):
            if f0 <= frame <= f1:
                u = (frame - f0) / (f1 - f0)
                return lerp(v0, v1, self.ease(u))

        raise AssertionError("unreachable: frame lies inside the span")


# --- channels ---------------------------------------------------------------


class Channel:
    """One parameter's timeline: an initial value plus the drivers covering it."""

    def __init__(self, name: str, spec: ChannelSpec, initial: Value):
        if len(initial) != spec.arity:
            raise ArityError(
                f"channel {name!r}: initial value needs {spec.arity} "
                f"component(s), got {len(initial)}"
            )
        self.name = name
        self.spec = spec
        self.initial = initial
        self.drivers: list = []

    def freeze(self) -> None:
        """Order the drivers by span. Overlap is allowed; it blends."""
        self.drivers.sort(key=lambda d: (d.start, d.end))

    def blend(self, frame: int) -> Union[Value, None]:
        """Weighted mean of every driver covering ``frame``.

        ``None`` when nothing is driving this channel there -- either no driver
        covers the frame, or the covering drivers' weights sum to zero.
        """
        total = 0.0
        accumulated = None

        for driver in self.drivers:
            if not (driver.start <= frame <= driver.end):
                continue
            weight = driver.weight(frame)
            if weight == 0.0:
                continue

            value = driver.evaluate(frame)[self.name]
            if accumulated is None:
                accumulated = [0.0] * len(value)
            elif len(value) != len(accumulated):
                raise ArityError(
                    f"channel {self.name!r}: drivers disagree on arity at frame "
                    f"{frame}, got {len(accumulated)} and {len(value)}"
                )
            for i, component in enumerate(value):
                accumulated[i] += component * weight
            total += weight

        if accumulated is None or total == 0.0:
            return None
        return tuple(component / total for component in accumulated)

    def sample(self, frame: int) -> Value:
        value = self.blend(frame)

        if value is None:
            # Nothing is driving the channel here, so hold the last value it was
            # actually driven to. Walk driver ends backwards and take the most
            # recent one that blends to something: a zero-weight driver is
            # skipped entirely, so it must not shadow an earlier real value.
            ended = sorted({d.end for d in self.drivers if d.end < frame}, reverse=True)
            for end in ended:
                value = self.blend(end)
                if value is not None:
                    break
        if value is None:
            value = self.initial

        if len(value) != self.spec.arity:
            raise ArityError(
                f"channel {self.name!r}: expected {self.spec.arity} "
                f"component(s) at frame {frame}, got {len(value)}"
            )
        return value


# --- timeline ---------------------------------------------------------------


@dataclass
class Segment:
    """An unplaced driver: a duration plus how to build it once positioned."""

    duration_secs: Union[float, None]
    duration_frames: Union[int, None]
    build: Callable[[int, int], Driver]


def _one_duration(secs, frames, fps: int, what: str) -> float:
    if (secs is None) == (frames is None):
        raise ValueError(f"{what}: pass exactly one of secs= or frames=")
    return float(secs) if secs is not None else frames / fps


class Timeline:
    """Absolute-time keyframe tracks, authored in seconds, evaluated on frames.

    Public entry points take a position or duration as either ``secs=`` or
    ``frames=``. Internally everything is integer frames.
    """

    def __init__(self, fps: int, initial: Dict[str, Value], resolve=None):
        if fps <= 0:
            raise ValueError(f"fps must be positive, got {fps}")
        if resolve is None:
            from . import channels

            resolve = channels.resolve

        self.fps = fps
        self.initial = {k: as_value(v) for k, v in initial.items()}
        self._resolve = resolve
        self._drivers: list = []
        self._cursor = 0.0  # seconds; only cumulative positions convert to frames

    # -- placement ----------------------------------------------------------

    def at(self, *, secs=None, frames=None) -> "Timeline":
        """Park the cursor at an absolute position."""
        self._cursor = _one_duration(secs, frames, self.fps, "at()")
        return self

    def add(self, driver: Driver) -> "Timeline":
        """Place a pre-built driver at its own absolute frames."""
        if driver.end < driver.start:
            raise ValueError(
                f"driver span ends before it starts: [{driver.start}, {driver.end}]"
            )
        self._drivers.append(driver)
        return self

    def then(self, *segments: Segment) -> "Timeline":
        """Place segments one after another, advancing the cursor after each."""
        for segment in segments:
            self._cursor += self._place(segment)
        return self

    def together(self, *segments: Segment) -> "Timeline":
        """Place segments from the same cursor; advance by the longest."""
        durations = [self._place(segment) for segment in segments]
        if durations:
            self._cursor += max(durations)
        return self

    def _place(self, segment: Segment) -> float:
        duration = _one_duration(
            segment.duration_secs, segment.duration_frames, self.fps, "segment"
        )
        start = round(self._cursor * self.fps)
        end = round((self._cursor + duration) * self.fps)
        self.add(segment.build(start, end))
        return duration

    # -- inspection ---------------------------------------------------------

    @property
    def last_frame(self) -> int:
        return max((d.end for d in self._drivers), default=0)

    def spans(self):
        """``(channel, start, end)`` for every driver, in placement order."""
        return [(name, d.start, d.end) for d in self._drivers for name in d.writes()]

    def describe(self) -> str:
        lines = [f"{self.fps} fps, {self.last_frame + 1} frames"]
        for name, start, end in self.spans():
            secs = (end - start) / self.fps
            lines.append(f"  {name:<24} frames {start:>5}-{end:<5} ({secs:g}s)")
        return "\n".join(lines)

    # -- evaluation ---------------------------------------------------------

    def _channels(self) -> Dict[str, Channel]:
        driven: Dict[str, list] = {}
        for driver in self._drivers:
            for name in driver.writes():
                driven.setdefault(name, []).append(driver)

        missing = sorted(name for name in driven if name not in self.initial)
        if missing:
            raise MissingInitialError(
                "no initial value declared for driven channel(s): "
                + ", ".join(repr(name) for name in missing)
            )

        built = {}
        for name, value in self.initial.items():
            channel = Channel(name, self._resolve(name), value)
            channel.drivers = driven.get(name, [])
            channel.freeze()
            built[name] = channel
        return built

    def sample(self, frame: int) -> Dict[str, Value]:
        """Every channel's value at ``frame``."""
        return {
            name: channel.sample(frame) for name, channel in self._channels().items()
        }

    def render(
        self,
        renderer,
        out_dir,
        prefix: str,
        *,
        drop_last: bool = False,
        flatten_alpha: bool = True,
        progress: Union[Callable[[int, int], None], None] = None,
    ) -> None:
        """Render every frame to ``out_dir/prefix_NNNN.png``.

        A channel is pushed to the renderer only when its value changes, so held
        channels cost nothing. Because nothing has been emitted yet at frame 0,
        that frame pushes every channel -- the rendered scene therefore matches
        the timeline's declared initial state whatever the scene setup left
        behind.

        ``drop_last`` omits the final frame, giving exactly ``duration * fps``
        frames; use it for a seamless loop, where the last frame would duplicate
        the first.
        """
        out_dir = Path(out_dir)
        out_dir.mkdir(parents=True, exist_ok=True)

        channels = self._channels()
        stop = self.last_frame if drop_last else self.last_frame + 1
        emitted: Dict[str, Value] = {}

        for frame in range(stop):
            for name, channel in channels.items():
                value = channel.sample(frame)
                if name not in emitted or emitted[name] != value:
                    channel.spec.setter(renderer, value)
                    emitted[name] = value

            path = out_dir / f"{prefix}_{frame:04d}.png"
            renderer.session(str(path))
            renderer.redraw()
            if flatten_alpha:
                flatten_alpha_channel(path)
            if progress is not None:
                progress(frame, stop)


def flatten_alpha_channel(path) -> None:
    """Rewrite a rendered PNG without its alpha channel.

    renderlib's headless ``PythonRenderer::redraw()`` passes
    ``backgroundAlpha = 0.0f``, so frames carry correct RGB -- already blended
    over the background colour -- but zero alpha wherever no ray hit anything.
    Dropping alpha yields the intended opaque image.
    """
    from PIL import Image

    with Image.open(path) as image:
        opaque = image.convert("RGB")
    opaque.save(path)
