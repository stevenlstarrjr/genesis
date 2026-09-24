"""Reflection-probe startup API validation."""
import genesis as gx

assert gx.configuration()["reflection_probes"] == []
index = gx.reflections.probe(
    "Room",
    position=(0, 2, 0),
    bounds_min=(-5, 0, -4),
    bounds_max=(5, 4, 4),
    blend_distance=1,
    priority=3,
)
assert index == 0
probe = gx.configuration()["reflection_probes"][0]
assert probe["name"] == "Room"
assert probe["position"] == [0, 2, 0]
assert probe["bounds_min"] == [-5, 0, -4]
assert probe["bounds_max"] == [5, 4, 4]
assert probe["blend_distance"] == 1
assert probe["priority"] == 3

def rejects(error, function):
    before = gx.configuration()["reflection_probes"]
    try:
        function()
        raise AssertionError("expected " + error.__name__)
    except error:
        pass
    assert gx.configuration()["reflection_probes"] == before

rejects(TypeError, lambda: gx.reflections.probe("", (0, 0, 0), (-1, -1, -1), (1, 1, 1)))
rejects(ValueError, lambda: gx.reflections.probe("Bad bounds", (0, 0, 0), (1, -1, -1), (-1, 1, 1)))
rejects(ValueError, lambda: gx.reflections.probe("Outside", (2, 0, 0), (-1, -1, -1), (1, 1, 1)))
rejects(ValueError, lambda: gx.reflections.probe("Blend", (0, 0, 0), (-1, -1, -1), (1, 1, 1), blend_distance=2))
rejects(TypeError, lambda: gx.reflections.probe("Priority", (0, 0, 0), (-1, -1, -1), (1, 1, 1), priority=1.5))

gx.reflections.clear()
assert gx.configuration()["reflection_probes"] == []
