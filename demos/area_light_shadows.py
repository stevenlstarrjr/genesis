"""Isolated area-light shadow regression with a floor and a box blocker."""
import genesis as gx

gx.window.configure(title="Genesis - Area Shadows", width=1280, height=720, vsync=False)
gx.renderer.quality("medium", gpu_timings=True)
gx.color.agx(look="neutral", exposure=0, auto_exposure=False)
gx.sky.earth().sun(azimuth=145, elevation=35, intensity=0).radiance(0)

gx.lights.area(
    "Shadowed panel",
    position=(-3, 6, 2),
    direction=(0.45, -0.83, -0.33),
    up=(0, 0, 1),
    color=(1, 0.82, 0.64),
    intensity=240,
    radius=13,
    width=3,
    height=2,
    shadow_bias=0.05,
    casts_shadows=True,
)

gx.camera.look_at(position=(1, 8, 12), target=(0, 0, 0), fov=48)
gx.camera.fly(speed=2)
gx.scene.load("assets/lighting/area-shadow-stage.gltf")
