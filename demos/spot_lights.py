"""Shadowed spot-light regression in Sponza: two colored pools with occluding geometry."""
import genesis as gx

gx.window.configure(title="Genesis - Spot Lights", width=1280, height=720, vsync=False)
gx.renderer.quality("medium", gpu_timings=True)
gx.color.agx(look="punchy", exposure=-1.0, auto_exposure=False)
gx.sky.earth().sun(azimuth=145, elevation=18, intensity=0.25)

gx.lights.spot(
    "Warm downlight",
    position=(0, 7.0, 0),
    direction=(0, -1.0, 0),
    color=(1.0, 0.55, 0.22),
    intensity=360,
    radius=12,
    inner_angle=20,
    outer_angle=22,
    shadow_bias=0.02,
)
gx.camera.look_at(position=(-10, 2.5, 0), target=(0, 2.5, 0), fov=60)
gx.camera.view("center", position=(-10, 2.5, 0), target=(0, 2.5, 0), fov=60)
gx.camera.fly(speed=2.5)
gx.scene.load("assets/glTF-Sample-Models/2.0/Sponza/glTF/Sponza.gltf")
