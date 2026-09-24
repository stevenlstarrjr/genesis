"""IES profile and projected spotlight-cookie regression on a dark plane."""
import genesis as gx

gx.window.configure(title="Genesis - IES and Light Cookies", width=1280, height=720, vsync=False)
gx.renderer.quality("high", gpu_timings=True)
gx.color.agx(look="neutral", exposure=0.0, auto_exposure=False)
gx.sky.earth().sun(azimuth=145, elevation=35, intensity=0).radiance(0)

gx.lights.spot(
    "BEGA IES wall light",
    position=(-5, 8.4, -5.35),
    direction=(0, -1, 0),
    up=(0, 0, 1),
    color=(1.0, 0.78, 0.55),
    intensity=850,
    radius=12,
    inner_angle=80,
    outer_angle=85,
    casts_shadows=False,
    ies="assets/ies/2fc3e48c6b767571aa1731eacda4c39e.ies",
)
gx.lights.spot(
    "Flashlight cookie",
    position=(5, 4.5, 1),
    direction=(0, 0, -1),
    up=(0, 1, 0),
    color=(0.45, 0.70, 1.0),
    intensity=700,
    radius=12,
    inner_angle=27,
    outer_angle=30,
    cookie="assets/light_cookie/300px-Cookie_tutorial_texture_flashlight.png",
)

gx.camera.look_at(position=(0, 5.0, 16), target=(0, 4.5, -6), fov=45)
gx.camera.view("patterns", position=(0, 5.0, 16), target=(0, 4.5, -6), fov=45)
gx.camera.fly(speed=2.0)
gx.scene.load("assets/lighting/light-stage.gltf")
