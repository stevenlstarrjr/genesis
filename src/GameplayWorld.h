#pragma once

#include <entt/entity/registry.hpp>
#include <array>
#include <cstdint>
#include <string>
#include <optional>
#include <unordered_set>
#include <vector>

namespace genesis::gameplay {

using EntityId = std::uint32_t;

struct Name {
    std::string value;
};

struct Transform {
    std::array<float, 3> position{0.0f, 0.0f, 0.0f};
    std::array<float, 3> rotation{0.0f, 0.0f, 0.0f}; // Euler degrees
    std::array<float, 3> scale{1.0f, 1.0f, 1.0f};
};

struct Tags {
    std::unordered_set<std::string> values;
};

struct Renderable {
    struct MeshVertexEdit {
        uint32_t index=0; // Flattened node/primitive vertex order in the source glTF.
        std::array<float,3> position{}; // Primitive-local position.
        bool operator==(const MeshVertexEdit&) const = default;
    };
    std::string path;
    bool visible = true;
    bool particleKinematic = false; // Follow entity position/rotation during Play.
    struct Material {
        std::array<float,3> color{1,1,1};
        float metallic=0, roughness=.5f;
        bool operator==(const Material&) const = default;
    };
    std::optional<Material> material;
    std::vector<MeshVertexEdit> meshVertices;
};
struct Light {
    enum class Type { Point, Spot, Area };
    Type type=Type::Point;
    bool enabled=true, shadows=true;
    std::array<float,3> color{1,1,1};
    float intensity=20, range=10, bias=.015f;
    float innerAngle=25, outerAngle=35, width=2, height=2;
    bool operator==(const Light&) const = default;
};
struct ReflectionProbe {
    bool enabled=true;
    std::array<float,3> size{10,10,10}; // Axis-aligned box centered on the entity.
    float blend=1;
    int priority=0;
    bool operator==(const ReflectionProbe&) const = default;
};
struct ParticleSystem {
    enum class Mode { Fluid, Cloth, Granular, Explosion, Emitter };
    enum class PinEdge { None, Left, Right, Both };
    struct Layer {
        enum class Shape { Soft, Spark };
        std::string name="Particles";
        Shape shape=Shape::Soft;
        int count=64;
        float lifetime=1.5f;
        float spawnRadius=.25f;
        std::array<float,3> offset{0,0,0};
        std::array<float,3> velocity{0,1,0};
        std::array<float,3> acceleration{0,0,0};
        float radialSpeed=0;
        float turbulence=.05f;
        float size=.08f;
        float sizeGrowth=0;
        float fadeIn=0; // Fraction of lifetime used to reach full opacity.
        std::array<float,4> startColor{1.f,.65f,.15f,.8f};
        std::array<float,4> endColor{1.f,.2f,.02f,0.f};
        bool operator==(const Layer&) const = default;
    };
    Mode mode=Mode::Fluid;
    PinEdge pinEdge=PinEdge::None; // Cloth X edges attach to the entity transform.
    bool enabled=true;
    // A rectangular particle seed. Cloth uses X by Z, one layer high.
    std::array<int,3> dimensions{12,8,12};
    float spacing=.15f;
    float mass=.02f;
    float friction=.4f;
    float damping=.01f;
    float viscosity=.01f;
    float cohesion=.05f;
    float stiffness=1000.f;
    float burstSpeed=9.f;
    float blastRadius=3.f;
    float blastImpulse=8.f;
    float effectDuration=2.5f;
    std::vector<Layer> layers{Layer{}}; // Visual emitter layers; physical modes ignore these.
    bool operator==(const ParticleSystem&) const = default;
};

struct EntitySnapshot {
    EntityId id{};
    std::string name;
    Transform transform;
    std::vector<std::string> tags;
    std::optional<Renderable> renderable;
    std::optional<Light> light;
    std::optional<ReflectionProbe> probe;
    std::optional<ParticleSystem> particles;
};

class GameplayWorld {
public:
    EntityId create(std::string name, const Transform& transform = {});
    bool destroy(EntityId id);
    [[nodiscard]] bool valid(EntityId id) const;
    [[nodiscard]] std::size_t size() const;
    void clear();
    // Clone authored components while retaining handles used by scripts/selection.
    void cloneFrom(const GameplayWorld& source);

    bool setName(EntityId id, std::string name);
    bool setTransform(EntityId id, const Transform& transform);
    bool setTag(EntityId id, std::string tag, bool enabled);
    bool setRenderable(EntityId id, std::string path);
    bool setVisible(EntityId id, bool visible);
    bool removeRenderable(EntityId id);
    bool setMaterial(EntityId id, std::optional<Renderable::Material> material);
    bool setMeshVertices(EntityId id, std::vector<Renderable::MeshVertexEdit> vertices);
    bool setParticleKinematic(EntityId id, bool enabled);
    bool setLight(EntityId id, std::optional<Light> light);
    bool setProbe(EntityId id, std::optional<ReflectionProbe> probe);
    bool setParticleSystem(EntityId id, std::optional<ParticleSystem> particles);
    void copyComponents(EntityId id, const EntitySnapshot& source);

    [[nodiscard]] bool snapshot(EntityId id, EntitySnapshot& result) const;
    [[nodiscard]] std::vector<EntitySnapshot> snapshots() const;

    [[nodiscard]] entt::registry& registry() { return m_registry; }
    [[nodiscard]] const entt::registry& registry() const { return m_registry; }

private:
    [[nodiscard]] static entt::entity entity(EntityId id);
    [[nodiscard]] static EntityId id(entt::entity entity);

    entt::registry m_registry;
};

extern GameplayWorld g_world;

} // namespace genesis::gameplay
