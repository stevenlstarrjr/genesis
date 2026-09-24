"""Original upward wall-leak view, retained for indoor lighting QA."""
import genesis as gx
gx.window.configure(title="Genesis - Upward wall regression", width=1280, height=720, vsync=False)
gx.renderer.quality("medium", gpu_timings=True)
gx.color.agx(look="punchy", exposure=-1.5, auto_exposure=True)
gx.color.auto_exposure(min_ev=-6, max_ev=6)
gx.sky.earth().sun(azimuth=145, elevation=42, intensity=5)
gx.reflections.probe("Sponza Interior", position=(0, 4, 0), bounds_min=(-13, 0, -9), bounds_max=(13, 13, 9), blend_distance=2, priority=10)
gx.entities.create("Runner", position=(2, 0, 0), rotation=(0, -90, 0)).tag("character").tag("player").model("assets/characters/character1.glb")
gx.camera.look_at(position=(-0.75, 1.45, 0), target=(-1.448, 1.865, 0.582), fov=46)
gx.camera.fly(speed=2.5)
gx.scene.load("assets/glTF-Sample-Models/2.0/Sponza/glTF/Sponza.gltf")
