"""Helmet material inspection. Add --debug-view material-ao to inspect baked AO."""
import genesis as gx

gx.window.configure(title="Genesis - Material AO", width=1280, height=720)
gx.renderer.quality("medium", gpu_timings=True)
gx.color.agx(look="neutral", exposure=0, auto_exposure=False)
gx.sky.earth().sun(intensity=0).radiance(0)
gx.sky.hdri("../assets/hdri/meadow_2_4k.exr", intensity=1)
gx.camera.look_at(position=(-2.2, 1.3, 3), target=(0, .1, 0), fov=45)
gx.camera.view("front", position=(0, .5, 3.8), target=(0, .1, 0), fov=45)
gx.camera.fly(speed=1)
gx.scene.load("../assets/glTF-Sample-Models/2.0/DamagedHelmet/glTF-Binary/DamagedHelmet.glb")
