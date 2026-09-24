"""Diffuse/specular HDRI check: roughness increases across each sphere row.

Gray and gold grids cover dielectric-to-metallic materials. No procedural sun,
sky ambient, local lights, or exposure adaptation are added to the meadow HDRI.
"""
import genesis as gx

gx.window.configure(title="Genesis - HDRI Material Test", width=1280, height=720, vsync=False)
gx.renderer.quality("medium", gpu_timings=True)
gx.color.agx(look="neutral", exposure=0, auto_exposure=False)
gx.sky.earth().sun(intensity=0).radiance(0)
gx.sky.hdri("../assets/hdri/meadow_2_4k.exr", intensity=1, rotation=0)
gx.camera.look_at(position=(0, 5.5, -20), target=(0, 3.4, 0), fov=36)
gx.camera.view("grazing", position=(-10, 5, -20), target=(0, 3.4, 0), fov=36)
gx.camera.fly(speed=2)
gx.entities.create("Material grid", position=(-3, 0, 1.5), scale=(1000, 1000, 1000)).model(
    "../assets/glTF-Sample-Models/2.0/MetalRoughSpheresNoTextures/glTF-Binary/MetalRoughSpheresNoTextures.glb")
