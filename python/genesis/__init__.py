"""Genesis startup API, executed by Genesis' embedded PocketPy interpreter.

Configure a scene before the SDL window opens. Distances use model/world units,
angles use degrees, color exposure uses EV. Entities use a native EnTT registry.
Native entry points prefixed with '_' are implementation details.
"""

import math as _math
import json as _json

__all__ = ["scene", "camera", "window", "renderer", "sky", "clouds", "color",
           "entities", "Entity", "reflections", "lights", "configuration"]


def configuration():
    """Return a detached snapshot of native startup settings, not live frame state."""
    return _json.loads(_configuration_json())


def _number(name, value, low, high):
    if type(value) not in (int, float):
        raise TypeError(name + " must be a number")
    if not _math.isfinite(value) or not low <= value <= high:
        raise ValueError(name + " must be finite and in [" + str(low) + ", " + str(high) + "]")
    return float(value)


def _integer(name, value, low, high):
    if type(value) is not int:
        raise TypeError(name + " must be an integer")
    _number(name, value, low, high)
    return value


def _boolean(name, value):
    if type(value) is not bool:
        raise TypeError(name + " must be True or False")
    return value


def _vector(name, value, low=-1e8, high=1e8):
    if not isinstance(value, (tuple, list)) or len(value) != 3:
        raise ValueError(name + " must contain exactly three numbers")
    return tuple([_number(name, component, low, high) for component in value])


class _Scene:
    """One glTF/GLB model per run. Relative paths resolve from the calling script."""
    def load(self, path=None):
        if not isinstance(path, str) or not path:
            raise TypeError("scene path must be a non-empty string")
        suffix = path.lower()
        if not (suffix.endswith(".gltf") or suffix.endswith(".glb")):
            raise ValueError("scene.load supports .gltf and .glb files")
        _scene_load(path)
        return self


class Entity:
    """Generation-safe handle to an entity in Genesis' native EnTT registry."""
    def __init__(self, entity_id):
        self.id = _integer("entity id", entity_id, 0, 4294967295)

    def valid(self):
        return _entity_valid(self.id)

    def snapshot(self):
        """Return a detached name, transform, and tag snapshot."""
        return _json.loads(_entity_snapshot_json(self.id))

    def rename(self, name):
        if not isinstance(name, str):
            raise TypeError("entity name must be a string")
        _entity_rename(self.id, name)
        return self

    def transform(self, position=None, rotation=None, scale=None):
        """Read or replace the transform; rotations are Euler degrees."""
        state = self.snapshot()["transform"]
        if position is None and rotation is None and scale is None:
            return state
        position = _vector("entity position", state["position"] if position is None else position)
        rotation = _vector("entity rotation", state["rotation"] if rotation is None else rotation)
        scale = _vector("entity scale", state["scale"] if scale is None else scale)
        _entity_transform(self.id, *position, *rotation, *scale)
        return self

    def tag(self, name, enabled=True):
        if not isinstance(name, str) or not name:
            raise ValueError("entity tag must be a non-empty string")
        _entity_tag(self.id, name, _boolean("tag enabled", enabled))
        return self

    def has_tag(self, name):
        if not isinstance(name, str) or not name:
            raise ValueError("entity tag must be a non-empty string")
        return name in self.snapshot()["tags"]

    def model(self, path, moving_collider=False):
        """Attach a glTF/GLB model when the render scene is created."""
        if not isinstance(path, str) or not path:
            raise TypeError("entity model path must be a non-empty string")
        suffix = path.lower()
        if not (suffix.endswith(".gltf") or suffix.endswith(".glb")):
            raise ValueError("entity model must be a .gltf or .glb file")
        _entity_model(self.id, path, _boolean("moving collider", moving_collider))
        return self

    def particles(self, mode="fluid", dimensions=None, spacing=0.15,
                  mass=0.02, friction=0.4, damping=0.01, viscosity=0.01,
                  cohesion=0.05, stiffness=1000.0, enabled=True, pin_edge="none",
                  burst_speed=9.0, blast_radius=3.0, blast_impulse=8.0,
                  effect_duration=2.5, layers=None):
        """Attach a visual emitter or PhysX fluid, cloth, granular, or explosion particles."""
        if mode is None:
            _entity_particles(self.id, "null")
            return self
        if mode not in ("fluid", "cloth", "granular", "explosion", "emitter", "campfire"):
            raise ValueError("particle mode must be emitter, fluid, cloth, granular, or explosion")
        if pin_edge not in ("none", "left", "right", "both"):
            raise ValueError("pin_edge must be none, left, right, or both")
        if dimensions is None:
            if mode == "cloth":
                dimensions = (12, 1, 12)
            elif mode == "explosion":
                dimensions = (8, 8, 8)
            else:
                dimensions = (12, 8, 12)
        if not isinstance(dimensions, (tuple, list)) or len(dimensions) != 3:
            raise ValueError("particle dimensions must contain three integers")
        dimensions = tuple([_integer("particle dimension", value, 1, 128) for value in dimensions])
        if dimensions[0] * dimensions[1] * dimensions[2] > 65536:
            raise ValueError("particle count exceeds 65536")
        if mode == "cloth" and dimensions[1] != 1:
            raise ValueError("cloth must have one layer")
        if mode == "cloth" and (dimensions[0] < 2 or dimensions[2] < 2):
            raise ValueError("cloth needs at least a 2 by 2 grid")
        data = {
            "mode": mode, "enabled": _boolean("particle enabled", enabled),
            "dimensions": dimensions, "spacing": _number("particle spacing", spacing, 0.01, 2),
            "mass": _number("particle mass", mass, 0.0001, 100),
            "friction": _number("particle friction", friction, 0, 1),
            "damping": _number("particle damping", damping, 0, 1),
            "viscosity": _number("particle viscosity", viscosity, 0, 100),
            "cohesion": _number("particle cohesion", cohesion, 0, 100),
            "stiffness": _number("cloth stiffness", stiffness, 0, 1000000),
            "pin_edge": pin_edge,
            "burst_speed": _number("burst speed", burst_speed, 0.1, 100),
            "blast_radius": _number("blast radius", blast_radius, 0.1, 100),
            "blast_impulse": _number("blast impulse", blast_impulse, 0, 100),
            "effect_duration": _number("effect duration", effect_duration, 0.1, 30),
        }
        if layers is not None:
            if mode not in ("emitter", "campfire") or not isinstance(layers, (tuple, list)):
                raise ValueError("layers must be a list for visual emitters")
            data["layers"] = layers
        _entity_particles(self.id, _json.dumps(data))
        return self

    def destroy(self):
        """Destroy the entity; False means this handle was already stale."""
        return _entity_destroy(self.id)


class _Entities:
    def create(self, name="", position=(0, 0, 0), rotation=(0, 0, 0), scale=(1, 1, 1)):
        if not isinstance(name, str):
            raise TypeError("entity name must be a string")
        position = _vector("entity position", position)
        rotation = _vector("entity rotation", rotation)
        scale = _vector("entity scale", scale)
        return Entity(_entity_create(name, *position, *rotation, *scale))

    def get(self, entity_id):
        return Entity(entity_id)

    def all(self):
        return [Entity(item["id"]) for item in _json.loads(_entities_snapshot_json())]

    def count(self):
        return len(_json.loads(_entities_snapshot_json()))


class _Camera:
    """Free-flight camera. No Sponza-specific position is hidden in C++."""
    def look_at(self, position=None, target=None, up=(0, 1, 0), fov=60):
        position = _vector("position", position)
        target = _vector("target", target)
        up = _vector("up", up)
        fov = _number("fov (degrees)", fov, 5, 150)
        direction = tuple([target[i] - position[i] for i in range(3)])
        cross = (direction[1]*up[2] - direction[2]*up[1],
                 direction[2]*up[0] - direction[0]*up[2],
                 direction[0]*up[1] - direction[1]*up[0])
        if sum([v*v for v in cross]) < 1e-12:
            raise ValueError("camera needs distinct position/target and a non-parallel, nonzero up vector")
        _camera_look_at(*position, *target, *up, fov)
        return self

    def fly(self, speed=1.5):
        """Walking speed in scene units/second. Hold Shift for 3x speed."""
        _camera_fly(_number("camera speed", speed, 0.001, 10000))
        return self

    def view(self, name, position=None, target=None, up=(0, 1, 0), fov=60):
        """Register a named startup view selectable with ``--camera name``."""
        if not isinstance(name, str) or not name:
            raise TypeError("camera view name must be a non-empty string")
        position = _vector("position", position)
        target = _vector("target", target)
        up = _vector("up", up)
        fov = _number("fov (degrees)", fov, 5, 150)
        direction = tuple([target[i] - position[i] for i in range(3)])
        cross = (direction[1]*up[2] - direction[2]*up[1],
                 direction[2]*up[0] - direction[0]*up[2],
                 direction[0]*up[1] - direction[1]*up[0])
        if sum([v*v for v in cross]) < 1e-12:
            raise ValueError("camera view needs distinct position/target and a non-parallel, nonzero up vector")
        _camera_view(name, *position, *target, *up, fov)
        return self


class _Window:
    def configure(self, title="Genesis", width=1600, height=900, vsync=True):
        if not isinstance(title, str) or not title:
            raise TypeError("window title must be a non-empty string")
        width = _integer("width", width, 320, 7680)
        height = _integer("height", height, 240, 4320)
        vsync = _boolean("vsync", vsync)
        _window_configure(title, width, height, vsync)
        return self


class _Renderer:
    """Startup quality settings retained for script compatibility.

    Genesis currently renders through its real-time bgfx raster pipeline.
    """
    def realtime(self, samples=1, restir_di=True, restir_gi=True,
                 denoise=True, taa=True, bloom=True, bounces=12,
                 diffuse_bounces=2, history_frames=25, specular_history_frames=40,
                 render_scale=1.0):
        samples = _integer("samples", samples, 1, 8)
        for name, value in (("restir_di", restir_di), ("restir_gi", restir_gi),
                            ("denoise", denoise), ("taa", taa), ("bloom", bloom)):
            _boolean(name, value)
        bounces = _integer("bounces", bounces, 1, 20)
        diffuse_bounces = _integer("diffuse_bounces", diffuse_bounces, 0, bounces)
        history_frames = _integer("history_frames", history_frames, 4, 30)
        specular_history_frames = _integer("specular_history_frames", specular_history_frames, 4, 60)
        render_scale = _number("render_scale", render_scale, 0.5, 1.0)
        if render_scale < 1.0 and not taa:
            raise ValueError("render_scale below 1 requires taa=True for temporal upscaling")
        if samples != 1 and (restir_di or restir_gi):
            raise ValueError("ReSTIR requires samples=1; disable restir_di and restir_gi for multiple samples")
        _renderer_realtime(samples, restir_di, restir_gi, denoise, taa, bloom,
                           bounces, diffuse_bounces, history_frames, specular_history_frames, render_scale)
        return self

    def reference(self, samples=1024, bounces=12):
        """Validate and retain legacy reference-quality startup settings."""
        samples = _integer("samples", samples, 1, 65536)
        bounces = _integer("bounces", bounces, 1, 20)
        _renderer_reference(samples, bounces)
        return self

    def quality(self, preset="medium", gpu_timings=True):
        """Select a coherent raster quality preset and timing-overlay visibility."""
        if not isinstance(preset, str):
            raise TypeError("renderer quality preset must be a string")
        if preset not in ("low", "medium", "high"):
            raise ValueError("renderer quality preset must be low, medium, or high")
        gpu_timings = _boolean("gpu_timings", gpu_timings)
        _renderer_quality(preset, gpu_timings)
        return self

    def debug_view(self, mode="none"):
        """Show screen-space AO, material AO, contact shadows, or isolated bloom."""
        if not isinstance(mode, str):
            raise TypeError("renderer debug view must be a string")
        if mode not in ("none", "ao", "contact", "material-ao", "bloom"):
            raise ValueError("renderer debug view must be none, ao, contact, material-ao, or bloom")
        _renderer_debug_view(mode)
        return self


class _Reflections:
    """Local box-projected reflection probes configured before rendering starts."""
    def probe(self, name, position=None, bounds_min=None, bounds_max=None, blend_distance=1.0, priority=0):
        if not isinstance(name, str) or not name:
            raise TypeError("probe name must be a non-empty string")
        position = _vector("probe position", position)
        bounds_min = _vector("probe bounds_min", bounds_min)
        bounds_max = _vector("probe bounds_max", bounds_max)
        for axis in range(3):
            if bounds_min[axis] >= bounds_max[axis]:
                raise ValueError("probe bounds_min must be less than bounds_max on every axis")
            if not bounds_min[axis] <= position[axis] <= bounds_max[axis]:
                raise ValueError("probe position must be inside its bounds")
        blend_distance = _number("probe blend_distance", blend_distance, 0.001, 100000)
        smallest_half_extent = min([(bounds_max[i] - bounds_min[i]) * 0.5 for i in range(3)])
        if blend_distance > smallest_half_extent:
            raise ValueError("probe blend_distance cannot exceed the smallest half extent")
        priority = _integer("probe priority", priority, -2147483648, 2147483647)
        return _reflection_probe_add(name, *position, *bounds_min, *bounds_max,
                                     blend_distance, priority)

    def clear(self):
        _reflection_probes_clear()
        return self


class _Lights:
    """Local lights configured before rendering starts."""
    def point(self, name, position=None, color=(1, 1, 1), intensity=20, radius=10,
              shadow_bias=0.015, casts_shadows=True, ies=None):
        if not isinstance(name, str) or not name:
            raise TypeError("point light name must be a non-empty string")
        position = _vector("point light position", position)
        color = _vector("point light color", color, 0, 100)
        intensity = _number("point light intensity", intensity, 0, 1000000)
        radius = _number("point light radius", radius, 0.001, 100000)
        shadow_bias = _number("point light shadow_bias", shadow_bias, 0, 1)
        if not isinstance(casts_shadows, bool):
            raise TypeError("point light casts_shadows must be a bool")
        if ies is not None and (not isinstance(ies, str) or not ies.lower().endswith(".ies")):
            raise ValueError("point light ies must be None or an .ies path")
        return _point_light_add(name, *position, *color, intensity, radius,
                                shadow_bias, casts_shadows, "" if ies is None else ies)

    def spot(self, name, position=None, direction=(0, -1, 0), color=(1, 1, 1),
             intensity=20, radius=10, inner_angle=25, outer_angle=35,
             shadow_bias=0.015, casts_shadows=True, up=(0, 0, 1),
             cookie=None, ies=None):
        if not isinstance(name, str) or not name:
            raise TypeError("spot light name must be a non-empty string")
        position = _vector("spot light position", position)
        direction = _vector("spot light direction", direction)
        color = _vector("spot light color", color, 0, 100)
        intensity = _number("spot light intensity", intensity, 0, 1000000)
        radius = _number("spot light radius", radius, 0.001, 100000)
        inner_angle = _number("spot light inner_angle", inner_angle, 0.001, 89.999)
        outer_angle = _number("spot light outer_angle", outer_angle, inner_angle, 89.999)
        shadow_bias = _number("spot light shadow_bias", shadow_bias, 0, 1)
        if not isinstance(casts_shadows, bool):
            raise TypeError("spot light casts_shadows must be a bool")
        up = _vector("spot light up", up)
        if cookie is not None:
            if not isinstance(cookie, str):
                raise ValueError("spot light cookie must be None or a supported image path")
            cookie_lower = cookie.lower()
            if not (cookie_lower.endswith(".png") or cookie_lower.endswith(".jpg") or
                    cookie_lower.endswith(".jpeg") or cookie_lower.endswith(".bmp") or
                    cookie_lower.endswith(".tga")):
                raise ValueError("spot light cookie must be None or a supported image path")
        if ies is not None and (not isinstance(ies, str) or not ies.lower().endswith(".ies")):
            raise ValueError("spot light ies must be None or an .ies path")
        return _spot_light_add(name, *position, *direction, *color, intensity,
                               radius, inner_angle, outer_angle, shadow_bias,
                               casts_shadows, *up, "" if cookie is None else cookie,
                               "" if ies is None else ies)

    def area(self, name, position=None, direction=(0, -1, 0), up=(0, 0, 1),
             color=(1, 1, 1), intensity=20, radius=10, width=2, height=2,
             shadow_bias=0.05, casts_shadows=True):
        """Add a one-sided rectangular emitter with optional center-projected shadows."""
        if not isinstance(name, str) or not name:
            raise TypeError("area light name must be a non-empty string")
        position = _vector("area light position", position)
        direction = _vector("area light direction", direction)
        up = _vector("area light up", up)
        color = _vector("area light color", color, 0, 100)
        intensity = _number("area light intensity", intensity, 0, 1000000)
        radius = _number("area light radius", radius, 0.001, 100000)
        width = _number("area light width", width, 0.001, 100000)
        height = _number("area light height", height, 0.001, 100000)
        shadow_bias = _number("area light shadow bias", shadow_bias, 0, 10)
        casts_shadows = _boolean("area light casts shadows", casts_shadows)
        return _area_light_add(name, *position, *direction, *up, *color,
                               intensity, radius, width, height, shadow_bias, casts_shadows)

    def clear(self):
        _point_lights_clear()
        return self

    def visualize(self, enabled=True, intensity=8, point_size=0.30,
                  spot_size=0.45, area_scale=1.0):
        """Draw optional emissive source meshes for all configured local lights."""
        enabled = _boolean("light source enabled", enabled)
        intensity = _number("light source intensity", intensity, 0, 1000000)
        point_size = _number("light source point_size", point_size, 0.001, 100000)
        spot_size = _number("light source spot_size", spot_size, 0.001, 100000)
        area_scale = _number("light source area_scale", area_scale, 0.001, 1000)
        _light_sources_configure(enabled, intensity, point_size, spot_size, area_scale)
        return self


class _Clouds:
    """Two sky cloud layers, baked into lighting/reflections. Distances in metres.

    Calls configure startup; wind animates natively after the script finishes.
    This is not local, fly-through weather or a rainfall simulation.
    """
    _kinds = ("cumulus", "stratus", "cirrus", "storm")
    _presets = ("clear", "partly_cloudy", "broken", "overcast", "storm", "high_clouds", "legacy")

    def types(self):
        return self._kinds

    def presets(self):
        return self._presets

    def _commit(self, state):
        _clouds_configure(_json.dumps(state))
        return self

    def preset(self, name="partly_cloudy"):
        if name not in self._presets:
            raise ValueError("unknown cloud preset; use clouds.presets()")
        state = configuration()["clouds"]
        state["configured"] = name != "legacy"
        if name == "legacy":
            return self._commit(state)
        low = {"kind": "cumulus", "enabled": False, "coverage": 0.4, "density": 1.4,
               "altitude": 1800, "thickness": 1400, "size": 4000, "detail": 0.5}
        high = {"kind": "cirrus", "enabled": False, "coverage": 0.2, "density": 0.3,
                "altitude": 9000, "thickness": 1200, "size": 8000, "detail": 0.7}
        state["enabled"] = name != "clear"
        if name == "partly_cloudy":
            low["enabled"] = True
            high["enabled"] = True
        elif name == "broken":
            low.update({"enabled": True, "coverage": 0.7, "density": 1.8, "thickness": 2200})
            high["enabled"] = True
        elif name == "overcast":
            low.update({"kind": "stratus", "enabled": True, "coverage": 1.0,
                        "density": 1.8, "altitude": 1000, "thickness": 1200, "detail": 0.15})
        elif name == "storm":
            low.update({"kind": "storm", "enabled": True, "coverage": 0.9,
                        "density": 3.5, "altitude": 800, "thickness": 5000, "size": 7000})
            high.update({"kind": "stratus", "enabled": True, "coverage": 0.9,
                         "density": 1.2, "altitude": 7000, "thickness": 1800})
        elif name == "high_clouds":
            high.update({"enabled": True, "coverage": 0.65, "density": 0.4})
        state["layers"] = [low, high]
        return self._commit(state)

    def clear(self):
        return self.preset("clear")

    def enable(self, enabled=True):
        enabled = _boolean("clouds enabled", enabled)
        state = configuration()["clouds"]
        state["configured"] = True
        state["enabled"] = enabled
        return self._commit(state)

    def layer(self, name="low", kind=None, coverage=None, density=None,
              altitude=None, thickness=None, size=None, detail=None, enabled=True):
        """Edit low/high layer; omitted appearance fields retain their values.

        Coverage is a shape control, not an exact percentage of visible sky.
        Calling layer enables the chosen layer unless enabled=False is supplied.
        """
        if name not in ("low", "high"):
            raise ValueError("cloud layer must be 'low' or 'high'")
        if kind is not None and kind not in self._kinds:
            raise ValueError("unknown cloud kind; use clouds.types()")
        enabled = _boolean("layer enabled", enabled)
        state = configuration()["clouds"]
        layer = state["layers"][0 if name == "low" else 1]
        if kind is not None:
            layer["kind"] = kind
        for field, value, lo, hi in (("coverage", coverage, 0, 1), ("density", density, 0, 8),
                ("altitude", altitude, 250, 20000), ("thickness", thickness, 100, 10000),
                ("size", size, 500, 40000), ("detail", detail, 0, 1)):
            if value is not None:
                layer[field] = _number("cloud " + field, value, lo, hi)
        layer["enabled"] = enabled
        for item in state["layers"]:
            if item["altitude"] + item["thickness"] > 25000:
                raise ValueError("cloud layer tops must be at or below 25000 metres")
        low, high = state["layers"]
        if low["enabled"] and high["enabled"] and low["altitude"] + low["thickness"] > high["altitude"]:
            raise ValueError("low and high cloud layers must be ordered and non-overlapping")
        state["configured"] = True
        if enabled:
            state["enabled"] = True
        return self._commit(state)

    def wind(self, speed=8, direction=45):
        """Speed in m/s, direction in horizontal sky-plane degrees; 0 freezes."""
        speed = _number("wind speed", speed, 0, 150)
        direction = _number("wind direction", direction, -360, 360)
        state = configuration()["clouds"]
        state["configured"] = True
        state["wind_speed"] = speed
        state["wind_direction"] = direction
        return self._commit(state)

    def quality(self, name="balanced", update_hz=2):
        """Low/balanced/high use 16/24/40 ray steps per layer during sky baking."""
        if name not in ("low", "balanced", "high"):
            raise ValueError("cloud quality must be low, balanced, or high")
        update_hz = _number("cloud update_hz", update_hz, 0.5, 10)
        state = configuration()["clouds"]
        state["configured"] = True
        state["quality"] = name
        state["update_hz"] = update_hz
        return self._commit(state)

    def seed(self, value=1):
        value = _integer("cloud seed", value, 0, 65535)
        state = configuration()["clouds"]
        state["configured"] = True
        state["seed"] = value
        return self._commit(state)


class _Sky:
    """Atmosphere plugin controls. Coefficients use inverse kilometers."""
    def earth(self):
        _atmosphere_earth()
        return self

    def hdri(self, path, intensity=1, rotation=0, visible=True):
        """Use an equirectangular EXR/HDR for the sky and image-based lighting."""
        if not isinstance(path, str) or not path:
            raise TypeError("HDRI path must be a non-empty string")
        lower_path = path.lower()
        if not (lower_path.endswith(".exr") or lower_path.endswith(".hdr")):
            raise ValueError("HDRI path must end in .exr or .hdr")
        intensity = _number("HDRI intensity", intensity, 0, 10000)
        rotation = _number("HDRI rotation", rotation, -360, 360)
        visible = _boolean("HDRI visible", visible)
        _sky_hdri(path, intensity, rotation, visible)
        return self

    def sun(self, azimuth=0, elevation=35, intensity=5):
        azimuth = _number("sun azimuth (degrees)", azimuth, -360, 360)
        elevation = _number("sun elevation (degrees)", elevation, -90, 90)
        intensity = _number("sun intensity", intensity, 0, 100000)
        _atmosphere_sun(azimuth, elevation, intensity)
        return self

    def radiance(self, multiplier=1):
        """Scale sky radiance, not display exposure. Use color.agx(exposure=EV)."""
        _atmosphere_exposure(_number("sky radiance multiplier", multiplier, 0, 10000))
        return self

    def time_of_day(self, time=-0.4, animate=False, speed=300):
        time = _number("time", time, -1, 1)
        animate = _boolean("animate", animate)
        speed = _number("day cycle speed", speed, 0, 100000)
        _atmosphere_time_of_day(time, animate, speed)
        return self

    def ground_albedo(self, red=0.30, green=0.15, blue=0.14):
        _atmosphere_ground_albedo(*_vector("ground albedo", (red, green, blue), 0, 1))
        return self

    def rayleigh(self, red=0.00580234, green=0.01355776, blue=0.03310001):
        _atmosphere_rayleigh(*_vector("Rayleigh coefficients", (red, green, blue), 0, 10))
        return self

    def aerosols(self, scattering=0.0014985, extinction=0.003996, anisotropy=0.8):
        scattering = _number("aerosol scattering", scattering, 0, 10)
        extinction = _number("aerosol extinction", extinction, scattering, 10)
        anisotropy = _number("anisotropy", anisotropy, -0.999, 0.999)
        _atmosphere_aerosols(scattering, extinction, anisotropy)
        return self

    def ozone(self, red=0.000650, green=0.001881, blue=0.000085):
        _atmosphere_ozone(*_vector("ozone coefficients", (red, green, blue), 0, 10))
        return self

    def radii(self, ground_km=6360, atmosphere_km=6420):
        ground_km = _number("ground radius (km)", ground_km, 1, 1e6)
        atmosphere_km = _number("atmosphere radius (km)", atmosphere_km, ground_km, 1e6)
        if atmosphere_km <= ground_km:
            raise ValueError("atmosphere radius must be greater than ground radius")
        _atmosphere_radii(ground_km, atmosphere_km)
        return self

    # Compatibility with earlier demos. 'radiance' is the unambiguous name.
    def exposure(self, radiance=1):
        return self.radiance(radiance)


class _Color:
    _looks = {"neutral": 0, "medium_high": 1, "punchy": 2, "golden": 3}

    def agx(self, look="neutral", exposure=0, auto_exposure=False):
        if look not in self._looks:
            raise ValueError("AgX look must be neutral, medium_high, punchy, or golden")
        exposure = _number("exposure (EV)", exposure, -12, 12)
        auto_exposure = _boolean("auto_exposure", auto_exposure)
        _color_agx(self._looks[look], exposure)
        _color_auto_exposure(auto_exposure, -6, 6)
        return self

    def auto_exposure(self, enabled=True, min_ev=-6, max_ev=6):
        enabled = _boolean("enabled", enabled)
        min_ev = _number("min_ev", min_ev, -12, 12)
        max_ev = _number("max_ev", max_ev, min_ev, 12)
        _color_auto_exposure(enabled, min_ev, max_ev)
        return self

    def aces(self, exposure=0):
        """Explicit opt-in compatibility transform; Genesis demos use AgX."""
        _color_aces(_number("exposure (EV)", exposure, -12, 12))
        return self


scene = _Scene()
entities = _Entities()
camera = _Camera()
window = _Window()
renderer = _Renderer()
reflections = _Reflections()
lights = _Lights()
sky = _Sky()
clouds = _Clouds()
sky.clouds = clouds
color = _Color()

# Compatibility aliases for existing scripts.
atmosphere = sky
load_model = scene.load
