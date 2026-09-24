"""Sponza: real-time raster PBR, AgX, and a Python-configured camera."""
import genesis as gx

gx.window.configure(title="Genesis - Sponza", width=1280, height=720, vsync=False)

# These startup quality settings remain accepted for compatibility while the
# current renderer uses its real-time bgfx raster pipeline.
gx.renderer.realtime(
    samples=1,
    restir_di=True,
    restir_gi=True,
    denoise=True,
    taa=True,
    bloom=False,
    bounces=4,
    diffuse_bounces=2,
    history_frames=30,
    specular_history_frames=50,
    render_scale=1.0,
)

# Adaptive exposure keeps the courtyard and galleries readable; the punchy
# look restores color separation without clipping the sunlit stone.
gx.color.agx(look="punchy", exposure=-1.5, auto_exposure=True)
gx.color.auto_exposure(min_ev=-6.0, max_ev=6.0)
gx.sky.earth().sun(azimuth=-25, elevation=42, intensity=5)
gx.camera.look_at(position=(-10, 2.5, 0), target=(0, 2.5, 0), fov=60)
gx.camera.fly(speed=1.5)
gx.scene.load("assets/glTF-Sample-Models/2.0/Sponza/glTF/Sponza.gltf")
