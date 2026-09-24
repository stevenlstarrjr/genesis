import genesis as gx

gx.window.configure(title="Genesis Hello Project", width=1280, height=720)
gx.renderer.quality("medium", gpu_timings=True)
# Keep material comparisons stable. AgX now encodes the display exactly once;
# no negative-EV workaround is needed for the previous double-gamma bug.
gx.color.agx(look="neutral", exposure=0, auto_exposure=False)
gx.camera.look_at(position=(0, 2.2, 6), target=(0, 1, 0), fov=50)
gx.sky.earth().sun(azimuth=145, elevation=35, intensity=2.5)
gx.sky.hdri("../../assets/hdri/meadow_2_4k.exr", intensity=1.0, rotation=0)
gx.scene.load("../../assets/glTF-Sample-Models/2.0/DamagedHelmet/glTF-Binary/DamagedHelmet.glb")
