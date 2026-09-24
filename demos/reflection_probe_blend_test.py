"""Two overlapping resident probes for local-to-local blend regression."""
import genesis as gx

gx.window.configure(title="Genesis - Reflection Probe Blend", width=1280, height=720, vsync=False)
gx.renderer.quality("medium", gpu_timings=True)
gx.color.agx(look="neutral", exposure=-2.0, auto_exposure=False)
gx.sky.earth().sun(azimuth=-35, elevation=38, intensity=5)
gx.lights.point("Left red", position=(-6, 7, 4), color=(1.0, 0.08, 0.03),
                intensity=70, radius=14, shadow_bias=0.02)
gx.lights.point("Right blue", position=(6, 7, 4), color=(0.03, 0.12, 1.0),
                intensity=70, radius=14, shadow_bias=0.02)
gx.reflections.probe(
    "Left Materials",
    position=(-4, 4, 0),
    bounds_min=(-12, -3, -12),
    bounds_max=(4, 12, 12),
    blend_distance=4,
    priority=10,
)
gx.reflections.probe(
    "Right Materials",
    position=(4, 4, 0),
    bounds_min=(-4, -3, -12),
    bounds_max=(12, 12, 12),
    blend_distance=4,
    priority=10,
)
gx.camera.look_at(position=(0, 4, 24), target=(0, 4, 0), fov=45)
gx.camera.view("blend-center", position=(0, 4, 24), target=(0, 4, 0), fov=45)
gx.camera.view("blend-left", position=(-6, 4, 20), target=(-3, 4, 0), fov=45)
gx.camera.view("blend-right", position=(6, 4, 20), target=(3, 4, 0), fov=45)
gx.camera.fly(speed=1.5)
gx.scene.load("assets/glTF-Sample-Models/2.0/MetalRoughSpheres/glTF-Binary/MetalRoughSpheres.glb")
