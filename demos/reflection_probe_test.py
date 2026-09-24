"""Metal/roughness and environment-probe visual regression."""
import genesis as gx

gx.window.configure(title="Genesis - Reflection Probe Test", width=1280, height=720, vsync=False)
gx.color.agx(look="neutral", exposure=-2.0, auto_exposure=False)
gx.sky.earth().sun(azimuth=-35, elevation=38, intensity=5)
gx.reflections.probe(
    "Material Test",
    position=(0, 4, 0),
    bounds_min=(-12, -3, -12),
    bounds_max=(12, 12, 12),
    blend_distance=2,
    priority=10,
)
gx.camera.look_at(position=(0, 4, 24), target=(0, 4, 0), fov=45)
gx.camera.fly(speed=1.5)
gx.scene.load("assets/glTF-Sample-Models/2.0/MetalRoughSpheres/glTF-Binary/MetalRoughSpheres.glb")
