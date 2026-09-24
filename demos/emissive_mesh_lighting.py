"""A glTF emissive panel illuminates a floor without authored scene lights."""
import genesis as gx

gx.window.configure(title="Genesis - Emissive Mesh Lighting", width=1280, height=720, vsync=False)
gx.renderer.realtime(bloom=True)
gx.renderer.quality("medium", gpu_timings=True)
gx.color.agx(look="neutral", exposure=-0.5, auto_exposure=False)
gx.sky.earth().sun(azimuth=145, elevation=35, intensity=0).radiance(0)

gx.camera.look_at(position=(0, 4, 9), target=(0, 1.2, 0), fov=48)
gx.camera.fly(speed=2)
gx.scene.load("assets/lighting/emissive-mesh-stage.gltf")
