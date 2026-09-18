"""The animatable channel namespace.

Each entry maps a channel name onto its arity and the single ``AgaveRenderer``
call that applies it. The namespace is hand-maintained: adding a new animatable
AGAVE command means adding an entry here.

Names are dotted and grouped by subsystem. Indexed families put the index on the
family root, so ``mat[0].opacity`` reads as "volume channel 0's opacity".

    resolve("camera.eye")      -> ChannelSpec(3, r.eye)
    resolve("mat[1].opacity")  -> ChannelSpec(1, r.mat_opacity bound to index 1)
"""

import re
from typing import Callable, Dict, List, Tuple

from .animation import ChannelSpec, UnknownChannelError

# Plain channels: name -> (arity, setter)
CHANNELS: Dict[str, ChannelSpec] = {
    "camera.eye": ChannelSpec(3, lambda r, v: r.eye(*v)),
    "camera.target": ChannelSpec(3, lambda r, v: r.target(*v)),
    "camera.up": ChannelSpec(3, lambda r, v: r.up(*v)),
    "camera.aperture": ChannelSpec(1, lambda r, v: r.aperture(*v)),
    "camera.focaldist": ChannelSpec(1, lambda r, v: r.focaldist(*v)),
    "camera.exposure": ChannelSpec(1, lambda r, v: r.exposure(*v)),
    "render.density": ChannelSpec(1, lambda r, v: r.density(*v)),
    "clip.plane": ChannelSpec(4, lambda r, v: r.set_clip_plane(*v)),
    "clip.region": ChannelSpec(6, lambda r, v: r.set_clip_region(*v)),
    "background.color": ChannelSpec(3, lambda r, v: r.background_color(*v)),
    "boundingbox.color": ChannelSpec(3, lambda r, v: r.bounding_box_color(*v)),
    "skylight.top": ChannelSpec(3, lambda r, v: r.skylight_top_color(*v)),
    "skylight.middle": ChannelSpec(3, lambda r, v: r.skylight_middle_color(*v)),
    "skylight.bottom": ChannelSpec(3, lambda r, v: r.skylight_bottom_color(*v)),
}

# Indexed families: templated name -> (arity, index -> setter)
INDEXED: Dict[str, Tuple[int, Callable[[int], Callable]]] = {
    "light[].pos": (3, lambda i: lambda r, v: r.light_pos(i, *v)),
    "light[].color": (3, lambda i: lambda r, v: r.light_color(i, *v)),
    "light[].size": (2, lambda i: lambda r, v: r.light_size(i, *v)),
    "mat[].diffuse": (4, lambda i: lambda r, v: r.mat_diffuse(i, *v)),
    "mat[].specular": (4, lambda i: lambda r, v: r.mat_specular(i, *v)),
    "mat[].emissive": (4, lambda i: lambda r, v: r.mat_emissive(i, *v)),
    "mat[].glossiness": (1, lambda i: lambda r, v: r.mat_glossiness(i, *v)),
    "mat[].opacity": (1, lambda i: lambda r, v: r.mat_opacity(i, *v)),
    "mat[].window_level": (2, lambda i: lambda r, v: r.set_window_level(i, *v)),
    "mat[].percentile": (
        2,
        lambda i: lambda r, v: r.set_percentile_threshold(i, *v),
    ),
    "mat[].isovalue": (
        2,
        lambda i: lambda r, v: r.set_isovalue_threshold(i, *v),
    ),
}

# Author-defined shorthands: alias name -> target channel name
ALIASES: Dict[str, str] = {}

_INDEXED = re.compile(r"^(\w+)\[(\d+)\]\.(.+)$")


def alias(name: str, target: str) -> None:
    """Register ``name`` as a shorthand for ``target``.

    The target is resolved immediately, so a typo fails here rather than at
    render time.
    """
    resolve(target)
    ALIASES[name] = target


def resolve(name: str) -> ChannelSpec:
    """Return the spec for a channel name, alias, or indexed channel."""
    seen = set()
    while name in ALIASES:
        if name in seen:
            raise UnknownChannelError(f"alias cycle involving {name!r}")
        seen.add(name)
        name = ALIASES[name]

    if name in CHANNELS:
        return CHANNELS[name]

    match = _INDEXED.match(name)
    if match:
        family, index, leaf = match.group(1), int(match.group(2)), match.group(3)
        key = f"{family}[].{leaf}"
        if key in INDEXED:
            arity, make_setter = INDEXED[key]
            return ChannelSpec(arity, make_setter(index))

    raise UnknownChannelError(f"unknown animation channel {name!r}")


def names() -> List[str]:
    """Every known channel name, with indexed families shown at index 0."""
    indexed = [key.replace("[]", "[0]") for key in INDEXED]
    return sorted(CHANNELS) + sorted(indexed)
