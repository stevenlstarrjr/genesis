"""HDR emissive-material and bloom regression with no scene lighting."""
import genesis as gx

gx.window.configure(title="Genesis - Emission and Bloom", width=1280, height=720, vsync=False)
gx.renderer.realtime(bloom=True)
gx.color.agx(look="neutral", exposure=0.0, auto_exposure=False)
gx.sky.earth().sun(azimuth=145, elevation=35, intensity=0).radiance(0)
gx.lights.clear()

gx.camera.look_at(position=(0, 5.5, 22), target=(0, 0, -1), fov=42)
gx.camera.view("emissive", position=(0, 5.5, 22), target=(0, 0, -1), fov=42)
gx.camera.fly(speed=2.0)
gx.scene.load(
    "assets/glTF-Sample-Models/2.0/EmissiveStrengthTest/glTF-Binary/EmissiveStrengthTest.glb"
)
