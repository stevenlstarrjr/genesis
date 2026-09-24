"""Viewport settings for the PhysX particle example."""
import genesis as gx

gx.window.configure(title="Genesis - PhysX Particles", width=1440, height=900)
gx.renderer.quality("medium")
gx.sky.earth().sun(azimuth=140, elevation=50, intensity=2).radiance(0.45)
gx.camera.look_at(position=(9, 8, 13), target=(0, 2, 0), fov=60)
