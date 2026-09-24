"""Rectangular area-light regression: one broad, one-sided studio panel."""
import genesis as gx

gx.window.configure(title="Genesis - Area Light", width=1280, height=720, vsync=False)
gx.renderer.quality("medium", gpu_timings=True)
gx.color.agx(look="neutral", exposure=-1.0, auto_exposure=False)
gx.sky.earth().sun(azimuth=145, elevation=25, intensity=0.15)

gx.lights.area(
    "Studio softbox",
    position=(0, 8, 4),
    direction=(0, -0.8944, -0.4472),
    up=(0, -0.4472, 0.8944),
    color=(1.0, 0.72, 0.48),
    intensity=260,
    radius=18,
    width=8,
    height=3,
)
gx.reflections.probe(
    "Studio",
    position=(0, 4, 0),
    bounds_min=(-10, -3, -8),
    bounds_max=(10, 12, 8),
    blend_distance=3,
    priority=10,
)

gx.camera.look_at(position=(0, 4, 18), target=(0, 4, 0), fov=45)
gx.camera.view("front", position=(0, 4, 18), target=(0, 4, 0), fov=45)
gx.camera.fly(speed=1.5)
gx.scene.load("assets/glTF-Sample-Models/2.0/MetalRoughSpheres/glTF-Binary/MetalRoughSpheres.glb")
