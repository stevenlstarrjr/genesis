"""HDRI diffuse uniforms must also reach both local-probe capture slots."""
import genesis as gx

gx.window.configure(title="HDRI probe capture regression", width=640, height=360, vsync=False)
gx.renderer.quality("medium", gpu_timings=True)
gx.color.agx(look="neutral", exposure=0, auto_exposure=False)
gx.sky.earth().sun(intensity=0).radiance(0)
gx.sky.hdri("../assets/hdri/meadow_2_4k.exr", intensity=.8, rotation=90)
gx.camera.look_at(position=(0, 1.5, 4), target=(0, .5, 0), fov=50)
gx.scene.load("../assets/glTF-Sample-Models/2.0/Box/glTF-Binary/Box.glb")
gx.reflections.probe("Left", position=(-1, 1, 1), bounds_min=(-6, -3, -6),
                    bounds_max=(6, 6, 6), blend_distance=1, priority=10)
gx.reflections.probe("Right", position=(1, 1, 1), bounds_min=(-6, -3, -6),
                    bounds_max=(6, 6, 6), blend_distance=1, priority=10)
