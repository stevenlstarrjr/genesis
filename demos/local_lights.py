"""Point-light regression in Sponza: warm and cool pools with GGX highlights."""
import genesis as gx

gx.window.configure(title="Genesis - Local Lights", width=1280, height=720, vsync=False)
gx.renderer.quality("medium", gpu_timings=True)
gx.color.agx(look="punchy", exposure=-1.0, auto_exposure=True)
gx.color.auto_exposure(min_ev=-6, max_ev=5)
gx.sky.earth().sun(azimuth=145, elevation=18, intensity=2)

gx.lights.point("Warm arch", position=(-4.5, 3.2, 0), color=(1.0, 0.32, 0.08), intensity=85, radius=9, shadow_bias=0.02)
gx.lights.point("Cool arch", position=(4.5, 3.2, 0), color=(0.10, 0.35, 1.0), intensity=85, radius=9, shadow_bias=0.02)
gx.lights.point("Center fill", position=(0, 5.5, 0), color=(1.0, 0.8, 0.55), intensity=55, radius=11, casts_shadows=False)

gx.camera.look_at(position=(-10, 2.5, 0), target=(0, 2.5, 0), fov=60)
gx.camera.view("center", position=(-10, 2.5, 0), target=(0, 2.5, 0), fov=60)
gx.camera.fly(speed=2.5)
gx.scene.load("assets/glTF-Sample-Models/2.0/Sponza/glTF/Sponza.gltf")
