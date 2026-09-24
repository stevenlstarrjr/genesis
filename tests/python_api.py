"""Run with genesis.exe --script tests/python_api.py --validate-script."""
import genesis as gx
import python_api_helper
assert python_api_helper.SENTINEL == 27
assert __file__.endswith("python_api.py")

checks = 0

def rejects(error, function):
    global checks
    before = gx.configuration()
    try:
        function()
    except error:
        assert gx.configuration() == before, "failed validation changed native settings"
        checks += 1
        return
    raise AssertionError("expected " + str(error))

assert gx.sky is gx.atmosphere
hero = gx.entities.create("hero", position=(1, 2, 3)).tag("player")
assert isinstance(hero, gx.Entity)
assert hero.valid()
assert hero.snapshot()["name"] == "hero"
assert hero.transform()["position"] == [1, 2, 3]
assert hero.has_tag("player")
assert hero.rename("wanderer").transform(rotation=(0, 90, 0), scale=(1.5, 1.5, 1.5)) is hero
assert hero.snapshot()["name"] == "wanderer"
assert hero.transform()["rotation"] == [0, 90, 0]
assert gx.entities.count() == 1
assert gx.entities.all()[0].id == hero.id
stale_id = hero.id
assert hero.destroy()
assert not hero.valid()
assert not hero.destroy()
replacement = gx.entities.create("replacement")
assert replacement.id != stale_id
assert replacement.model("assets/characters/character1.glb") is replacement
assert replacement.snapshot()["model"].endswith("assets/characters/character1.glb")
assert replacement.model("assets/characters/character1.glb", moving_collider=True).snapshot()["particle_collider"]["moving"] is True
assert replacement.particles("fluid", dimensions=(3, 4, 5)).snapshot()["particle_system"]["dimensions"] == [3, 4, 5]
assert replacement.particles("cloth").snapshot()["particle_system"]["mode"] == "cloth"
assert replacement.particles("cloth", pin_edge="left").snapshot()["particle_system"]["pin_edge"] == "left"
assert replacement.particles("granular").snapshot()["particle_system"]["mode"] == "granular"
assert replacement.particles("explosion", burst_speed=11, blast_radius=4).snapshot()["particle_system"]["burst_speed"] == 11
emitter = replacement.particles("emitter", layers=[{"name": "Mist", "count": 24,
                                                    "velocity": [0, 1, 0]}]).snapshot()["particle_system"]
assert emitter["mode"] == "emitter"
assert emitter["layers"][0]["name"] == "Mist"
assert emitter["layers"][0]["count"] == 24
assert replacement.particles("campfire").snapshot()["particle_system"]["mode"] == "emitter"
assert "particle_system" not in replacement.particles(None).snapshot()
rejects(ValueError, lambda: replacement.particles("cloth", dimensions=(4, 2, 4)))
rejects(ValueError, lambda: replacement.particles("cloth", dimensions=(1, 1, 4)))
rejects(ValueError, lambda: replacement.particles("explosion", blast_radius=0))
rejects(ValueError, lambda: hero.snapshot())
rejects(ValueError, lambda: replacement.tag(""))
rejects(ValueError, lambda: replacement.model("assets/missing.glb"))
assert gx.renderer.realtime() is gx.renderer
assert gx.renderer.realtime(samples=2, restir_di=False, restir_gi=False) is gx.renderer
assert gx.renderer.reference(samples=16) is gx.renderer
assert gx.renderer.quality("low", gpu_timings=False) is gx.renderer
assert gx.configuration()["renderer"]["quality"] == "low"
assert gx.configuration()["renderer"]["gpu_timings"] is False
assert gx.renderer.quality("medium") is gx.renderer
assert gx.renderer.debug_view("ao").debug_view() is gx.renderer
assert gx.renderer.debug_view("material-ao").debug_view() is gx.renderer
assert gx.renderer.debug_view("bloom").debug_view() is gx.renderer
assert gx.lights.point("warm", (1, 3, 2), color=(1, 0.5, 0.25), intensity=40, radius=8,
                       ies="assets/ies/2fc3e48c6b767571aa1731eacda4c39e.ies") == 0
assert gx.configuration()["point_lights"][0]["name"] == "warm"
assert gx.configuration()["point_lights"][0]["radius"] == 8
assert abs(gx.configuration()["point_lights"][0]["shadow_bias"] - 0.015) < 1e-6
assert gx.configuration()["point_lights"][0]["casts_shadows"] is True
assert gx.configuration()["point_lights"][0]["ies"].endswith("2fc3e48c6b767571aa1731eacda4c39e.ies")
rejects(ValueError, lambda: gx.lights.point("warm", (0, 1, 0)))
rejects(ValueError, lambda: gx.lights.point("bad-color", (0, 1, 0), color=(-1, 1, 1)))
rejects(ValueError, lambda: gx.lights.point("bad-radius", (0, 1, 0), radius=0))
rejects(ValueError, lambda: gx.lights.point("bad-position", None))
rejects(ValueError, lambda: gx.lights.point("bad-bias", (0, 1, 0), shadow_bias=2))
rejects(TypeError, lambda: gx.lights.point("bad-shadow-flag", (0, 1, 0), casts_shadows=1))
rejects(ValueError, lambda: gx.lights.point("missing-ies", (0, 1, 0), ies="assets/lighting/missing.ies"))
assert gx.lights.spot("stage", (0, 4, 0), direction=(0, -2, 0), color=(0.5, 0.75, 1),
                      intensity=60, radius=12, inner_angle=20, outer_angle=35,
                      shadow_bias=0.02, up=(0, 0, 2),
                      cookie="assets/light_cookie/300px-Cookie_tutorial_texture_flashlight.png") == 0
spot = gx.configuration()["spot_lights"][0]
assert spot["name"] == "stage"
assert spot["direction"] == [0, -1, 0]
assert spot["inner_angle"] == 20 and spot["outer_angle"] == 35
assert spot["casts_shadows"] is True
assert spot["up"] == [0, 0, 1] and spot["cookie"].endswith("300px-Cookie_tutorial_texture_flashlight.png")
rejects(ValueError, lambda: gx.lights.spot("zero-direction", (0, 1, 0), direction=(0, 0, 0)))
rejects(ValueError, lambda: gx.lights.spot("bad-cone", (0, 1, 0), inner_angle=40, outer_angle=20))
rejects(ValueError, lambda: gx.lights.spot("wide-cone", (0, 1, 0), outer_angle=90))
rejects(TypeError, lambda: gx.lights.spot("bad-spot-shadow", (0, 1, 0), casts_shadows=1))
rejects(ValueError, lambda: gx.lights.spot("parallel-spot-up", (0, 1, 0), up=(0, -2, 0)))
rejects(ValueError, lambda: gx.lights.spot("bad-cookie", (0, 1, 0), cookie="cookie.txt"))
assert gx.lights.area("panel", (0, 5, 0), direction=(0, -2, 0), up=(0, 0, 3),
                      color=(1, 0.8, 0.6), intensity=80, radius=14,
                      width=4, height=2) == 0
area = gx.configuration()["area_lights"][0]
assert area["name"] == "panel"
assert area["direction"] == [0, -1, 0] and area["up"] == [0, 0, 1]
assert area["width"] == 4 and area["height"] == 2
assert gx.lights.visualize(intensity=12, point_size=0.2, spot_size=0.3, area_scale=0.9) is gx.lights
sources = gx.configuration()["light_sources"]
assert sources["enabled"] is True and sources["intensity"] == 12
assert abs(sources["point_size"] - 0.2) < 1e-6
assert abs(sources["spot_size"] - 0.3) < 1e-6
assert abs(sources["area_scale"] - 0.9) < 1e-6
rejects(TypeError, lambda: gx.lights.visualize(enabled=1))
rejects(ValueError, lambda: gx.lights.visualize(point_size=0))
rejects(ValueError, lambda: gx.lights.visualize(area_scale=1001))
rejects(ValueError, lambda: gx.lights.area("zero-area-direction", (0, 1, 0), direction=(0, 0, 0)))
rejects(ValueError, lambda: gx.lights.area("parallel-area-up", (0, 1, 0), direction=(0, -1, 0), up=(0, 2, 0)))
rejects(ValueError, lambda: gx.lights.area("bad-area-width", (0, 1, 0), width=0))
assert gx.camera.look_at((1, 2, 3), (0, 2, 0)).fly(0.5) is gx.camera
assert gx.camera.view("hall", (1, 2, 3), (0, 2, 0)) is gx.camera
assert gx.camera.look_at(position=(-10, 2.5, 0), target=(0, 2.5, 0), fov=60) is gx.camera
assert gx.window.configure(width=800, height=600, vsync=False) is gx.window
assert gx.color.agx().auto_exposure(False) is gx.color
assert gx.sky.hdri("assets/hdri/meadow_2_4k.exr", intensity=0.8,
                   rotation=25, visible=False) is gx.sky
hdri = gx.configuration()["sky"]
assert hdri["hdri"].endswith("assets/hdri/meadow_2_4k.exr")
assert abs(hdri["hdri_intensity"] - 0.8) < 1e-6
assert hdri["hdri_rotation"] == 25
assert hdri["hdri_visible"] is False
assert gx.sky.earth().sun().radiance(1).ground_albedo().rayleigh().aerosols().ozone().radii() is gx.sky
assert gx.configuration()["camera"]["position"] == [-10, 2.5, 0]
assert gx.configuration()["camera"]["views"] == ["hall"]
assert gx.configuration()["renderer"]["debug_view"] == "none"
assert gx.configuration()["window"]["width"] == 800
assert gx.configuration()["renderer"]["mode"] == "reference"
snapshot = gx.configuration()
snapshot["camera"]["speed"] = 999
assert gx.configuration()["camera"]["speed"] == 0.5
rejects(ValueError, lambda: gx.renderer.realtime(samples=2))
rejects(ValueError, lambda: gx.renderer.realtime(samples=2, restir_di=False))
rejects(TypeError, lambda: gx.renderer.realtime(samples=True))
rejects(TypeError, lambda: gx.renderer.realtime(denoise=1))
rejects(ValueError, lambda: gx.renderer.realtime(bounces=2, diffuse_bounces=3))
rejects(ValueError, lambda: gx.renderer.realtime(history_frames=0))
rejects(ValueError, lambda: gx.renderer.realtime(specular_history_frames=61))
rejects(ValueError, lambda: gx.renderer.realtime(render_scale=0.49))
rejects(ValueError, lambda: gx.renderer.realtime(render_scale=1.1))
rejects(ValueError, lambda: gx.renderer.realtime(render_scale=float("nan")))
rejects(TypeError, lambda: gx.renderer.realtime(render_scale=True))
rejects(ValueError, lambda: gx.renderer.realtime(render_scale=0.75, taa=False))
assert gx.renderer.realtime(render_scale=0.75) is gx.renderer
assert gx.configuration()["renderer"]["render_scale"] == 0.75
gx.renderer.reference()
assert gx.configuration()["renderer"]["render_scale"] == 1
gx.renderer.realtime()
assert gx.configuration()["renderer"]["render_scale"] == 1
rejects(ValueError, lambda: gx.renderer.reference(samples=0))
rejects(TypeError, lambda: gx.renderer.reference(samples=1.5))
rejects(ValueError, lambda: gx.renderer.quality("ultra"))
rejects(TypeError, lambda: gx.renderer.quality(2))
rejects(TypeError, lambda: gx.renderer.quality("high", gpu_timings=1))
rejects(ValueError, lambda: gx.renderer.debug_view("normals"))
rejects(TypeError, lambda: gx.renderer.debug_view(1))
rejects(ValueError, lambda: gx.camera.look_at((0, 0, 0), (0, 0, 0)))
rejects(ValueError, lambda: gx.camera.look_at((0, 0, 0), (0, 1, 0)))
rejects(ValueError, lambda: gx.camera.look_at((0, 0), (1, 0, 0)))
rejects(ValueError, lambda: gx.camera.look_at((0, 0, 0), (1, 0, 0), fov=180))
rejects(ValueError, lambda: gx.camera.view("hall", (1, 2, 3), (0, 2, 0)))
rejects(ValueError, lambda: gx.camera.view("bad", (0, 0, 0), (0, 0, 0)))
rejects(ValueError, lambda: gx.camera.fly(float("nan")))
rejects(ValueError, lambda: gx.camera.fly(-1))
rejects(ValueError, lambda: gx.window.configure(width=0))
rejects(TypeError, lambda: gx.window.configure(vsync=1))
rejects(ValueError, lambda: gx.color.agx(look="unknown"))
rejects(ValueError, lambda: gx.color.agx(exposure=13))
rejects(ValueError, lambda: gx.color.agx(exposure=float("inf")))
rejects(ValueError, lambda: gx.color.auto_exposure(min_ev=3, max_ev=2))
rejects(ValueError, lambda: gx.sky.sun(elevation=91))
rejects(ValueError, lambda: gx.sky.radiance(-1))
rejects(ValueError, lambda: gx.sky.hdri("assets/hdri/meadow_2_4k.exr", intensity=-1))
rejects(ValueError, lambda: gx.sky.hdri("assets/hdri/meadow_2_4k.exr", rotation=361))
rejects(TypeError, lambda: gx.sky.hdri("assets/hdri/meadow_2_4k.exr", visible=1))
rejects(ValueError, lambda: gx.sky.hdri("assets/hdri/missing.exr"))
rejects(ValueError, lambda: gx.sky.hdri("assets/hdri/meadow_2_4k.png"))
rejects(ValueError, lambda: gx.sky.radii(ground_km=100, atmosphere_km=50))
rejects(ValueError, lambda: gx.sky.aerosols(scattering=2, extinction=1))
rejects(ValueError, lambda: gx.sky.ground_albedo(red=2))
rejects(ValueError, lambda: gx.scene.load("assets/missing.gltf"))
rejects(ValueError, lambda: gx.scene.load("assets/unsupported.obj"))
rejects(TypeError, lambda: gx.scene.load(42))
gx.scene.load(path="assets/glTF-Sample-Models/2.0/Sponza/glTF/Sponza.gltf")
rejects(ValueError, lambda: gx.scene.load("assets/glTF-Sample-Models/2.0/Sponza/glTF/Sponza.gltf"))
print("PASS: Python API valid calls and " + str(checks) + " rejected invalid configurations")
