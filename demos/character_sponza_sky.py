"""Animated character in Sponza with the Genesis atmosphere and clouds."""
import genesis as gx

gx.window.configure(title="Genesis - Character, Sponza, and Sky", width=1280, height=720, vsync=False)
gx.renderer.realtime(
    samples=1,
    restir_di=True,
    restir_gi=True,
    denoise=True,
    taa=True,
    bloom=False,
    bounces=3,
    diffuse_bounces=2,
    history_frames=12,
    specular_history_frames=20,
    render_scale=1.0,
)
gx.renderer.quality("medium", gpu_timings=True)
gx.color.agx(look="punchy", exposure=-1.5, auto_exposure=True)
gx.color.auto_exposure(min_ev=-6, max_ev=6)
# Key the runner from the camera side so skin detail remains readable while
# the open courtyard and procedural sky provide the fill light.
gx.sky.earth().sun(azimuth=145, elevation=42, intensity=5)
gx.clouds.preset("partly_cloudy")
gx.clouds.layer("low", coverage=0.45, density=1.4)
gx.clouds.wind(speed=8, direction=45)
gx.clouds.quality("balanced", update_hz=2)
gx.clouds.seed(7)

# Capture the Sponza interior for box-projected glossy reflections. Surfaces
# fade back to the global Bruneton sky probe near the volume boundary.
gx.reflections.probe(
    "Sponza Interior",
    position=(0, 4, 0),
    bounds_min=(-13, 0, -9),
    bounds_max=(13, 13, 9),
    blend_distance=2,
    priority=10,
)

# The GLB contains Armature|running|baselayer; Donut loops imported animations.
character = gx.entities.create(
    "Runner",
    position=(2, 0, 0),
    rotation=(0, -90, 0),
).tag("character").tag("player").model("assets/characters/character1.glb")

gx.camera.look_at(position=(-0.75, 1.45, 0), target=(2, 0.85, 0), fov=46)
gx.camera.fly(speed=2.5)
gx.scene.load("assets/glTF-Sample-Models/2.0/Sponza/glTF/Sponza.gltf")
