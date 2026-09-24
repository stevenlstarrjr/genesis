"""Point, spot, and area lights isolated on a dark ground plane."""
import genesis as gx

gx.window.configure(title="Genesis - Local Light Showcase", width=1280, height=720, vsync=False)
gx.renderer.realtime(bloom=True)
gx.renderer.quality("high", gpu_timings=True)
gx.color.agx(look="neutral", exposure=0.0, auto_exposure=False)

# A zero-radiance environment means no sky/ground ambient or image-based light.
# A zero-intensity sun contributes no directional light.
gx.sky.earth().sun(azimuth=145, elevation=35, intensity=0).radiance(0)

gx.lights.point(
    "Red point",
    position=(-6, 2.6, 0),
    color=(1.0, 0.035, 0.015),
    intensity=115,
    radius=5.2,
    shadow_bias=0.012,
)
gx.lights.spot(
    "Blue spot",
    position=(0, 6.0, 0),
    direction=(0, -1, 0),
    color=(0.025, 0.18, 1.0),
    intensity=520,
    radius=9,
    inner_angle=12,
    outer_angle=16,
    shadow_bias=0.012,
)
gx.lights.area(
    "Warm area",
    position=(6, 2.5, 0),
    direction=(0, -1, 0),
    up=(0, 0, 1),
    color=(1.0, 0.48, 0.10),
    intensity=70,
    radius=6.5,
    width=5.5,
    height=2.5,
)
gx.lights.visualize(intensity=12, point_size=0.28, spot_size=0.38)

gx.camera.look_at(position=(0, 10.5, 16), target=(0, 0, 0), fov=48)
gx.camera.view("lights", position=(0, 10.5, 16), target=(0, 0, 0), fov=48)
gx.camera.fly(speed=2.0)
gx.scene.load("assets/lighting/ground-plane.gltf")
