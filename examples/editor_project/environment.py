"""Lighting/game configuration stays in Python; the editor saves mesh entities separately."""
import genesis as gx

gx.window.configure(title="Genesis - Starter World", width=1440, height=900, vsync=True)
gx.renderer.quality("medium", gpu_timings=True)
gx.color.agx(look="neutral", exposure=0, auto_exposure=False)
gx.sky.earth().sun(azimuth=145, elevation=45, intensity=2).radiance(0.45)
gx.camera.look_at(position=(7, 5, -9), target=(0, 1, 0), fov=50)
gx.camera.fly(speed=4)
