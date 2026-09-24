"""Change the preset or layer controls to design your sky.

Run: build/release/microsoft/genesis.exe --script demos/clouds.py
"""
import genesis as gx

gx.window.configure(title="Genesis - Clouds", width=1600, height=900, vsync=False)
gx.renderer.realtime(render_scale=0.75)
# Adapt when walking from a sky view into the courtyard. Fixed 0 EV was only
# suitable for the original sky-only camera and underexposed indoor walls.
gx.color.agx(look="neutral", exposure=-1.5, auto_exposure=True)
gx.color.auto_exposure(min_ev=-6, max_ev=6)
gx.sky.earth().sun(azimuth=-25, elevation=35, intensity=5)
gx.clouds.preset("partly_cloudy")
gx.clouds.layer("low", coverage=0.45, density=1.4)
gx.clouds.wind(speed=8, direction=45)
gx.clouds.quality("balanced", update_hz=2)
gx.clouds.seed(7)
gx.camera.look_at(position=(-2, 3, 0), target=(8, 4, 0), fov=65)
gx.camera.fly(speed=1.5)
gx.scene.load("assets/glTF-Sample-Models/2.0/Sponza/glTF/Sponza.gltf")
