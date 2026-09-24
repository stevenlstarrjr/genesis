"""Dark isometric action-RPG presentation and performance preset."""
import genesis as gx

gx.window.configure(title="Genesis - Isometric Action RPG", width=1280, height=720, vsync=False)

# An 85% trace preserves more detail than the general performance preset while
# leaving useful GPU headroom for animated characters, effects, and props.
gx.renderer.realtime(
    samples=1,
    restir_di=True,
    restir_gi=True,
    denoise=True,
    taa=True,
    bloom=False,
    bounces=3,
    diffuse_bounces=2,
    history_frames=30,
    specular_history_frames=50,
    render_scale=0.85,
)

# A darker, higher-contrast grade gives the scene an action-RPG silhouette
# hierarchy without crushing the shaded walkways.
gx.color.agx(look="punchy", exposure=-0.5, auto_exposure=True)
gx.color.auto_exposure(min_ev=-4.0, max_ev=4.0)
gx.sky.earth().sun(azimuth=-35, elevation=42, intensity=5)

# High three-quarter camera typical of an isometric action game.
gx.camera.look_at(position=(-5, 5, 0), target=(2, 0.5, 0), fov=46)
gx.camera.fly(speed=2.5)
gx.scene.load("assets/glTF-Sample-Models/2.0/Sponza/glTF/Sponza.gltf")
