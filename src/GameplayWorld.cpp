#include "GameplayWorld.h"

#include <algorithm>
#include <utility>

namespace genesis::gameplay {

GameplayWorld g_world;

entt::entity GameplayWorld::entity(EntityId value) {
    return static_cast<entt::entity>(value);
}

EntityId GameplayWorld::id(entt::entity value) {
    return static_cast<EntityId>(entt::to_integral(value));
}

EntityId GameplayWorld::create(std::string name, const Transform& transform) {
    const auto value = m_registry.create();
    m_registry.emplace<Name>(value, std::move(name));
    m_registry.emplace<Transform>(value, transform);
    m_registry.emplace<Tags>(value);
    return id(value);
}

bool GameplayWorld::destroy(EntityId value) {
    const auto candidate = entity(value);
    if (!m_registry.valid(candidate)) return false;
    m_registry.destroy(candidate);
    return true;
}

bool GameplayWorld::valid(EntityId value) const {
    return m_registry.valid(entity(value));
}

std::size_t GameplayWorld::size() const {
    const auto* storage = m_registry.storage<Name>();
    return storage ? storage->size() : 0u;
}

void GameplayWorld::clear() {
    m_registry.clear();
}
void GameplayWorld::cloneFrom(const GameplayWorld& source) {
    if(this==&source)return;
    entt::registry empty;m_registry.swap(empty);
    for(const auto& snapshot:source.snapshots()) {
        const auto value=m_registry.create(entity(snapshot.id));
        m_registry.emplace<Name>(value,snapshot.name);
        m_registry.emplace<Transform>(value,snapshot.transform);
        m_registry.emplace<Tags>(value);
        for(const auto& tag:snapshot.tags)setTag(id(value),tag,true);
        copyComponents(id(value),snapshot);
    }
}

bool GameplayWorld::setName(EntityId value, std::string name) {
    const auto candidate = entity(value);
    if (!m_registry.valid(candidate)) return false;
    m_registry.get<Name>(candidate).value = std::move(name);
    return true;
}

bool GameplayWorld::setTransform(EntityId value, const Transform& transform) {
    const auto candidate = entity(value);
    if (!m_registry.valid(candidate)) return false;
    m_registry.replace<Transform>(candidate, transform);
    return true;
}

bool GameplayWorld::setTag(EntityId value, std::string tag, bool enabled) {
    const auto candidate = entity(value);
    if (!m_registry.valid(candidate)) return false;
    auto& tags = m_registry.get<Tags>(candidate).values;
    if (enabled) tags.emplace(std::move(tag));
    else tags.erase(tag);
    return true;
}

bool GameplayWorld::setRenderable(EntityId value, std::string path) {
    const auto candidate = entity(value);
    if (!m_registry.valid(candidate)) return false;
    m_registry.emplace_or_replace<Renderable>(candidate, std::move(path));
    return true;
}

bool GameplayWorld::snapshot(EntityId value, EntitySnapshot& result) const {
    const auto candidate = entity(value);
    if (!m_registry.valid(candidate)) return false;
    result.id = value;
    result.name = m_registry.get<Name>(candidate).value;
    result.transform = m_registry.get<Transform>(candidate);
    const auto& values = m_registry.get<Tags>(candidate).values;
    result.tags.assign(values.begin(), values.end());
    std::sort(result.tags.begin(), result.tags.end());
    if (const auto* renderable = m_registry.try_get<Renderable>(candidate))
        result.renderable = *renderable;
    else
        result.renderable.reset();
    result.light.reset();result.probe.reset();result.particles.reset();
    if(const auto* light=m_registry.try_get<Light>(candidate))result.light=*light;
    if(const auto* probe=m_registry.try_get<ReflectionProbe>(candidate))result.probe=*probe;
    if(const auto* particles=m_registry.try_get<ParticleSystem>(candidate))result.particles=*particles;
    return true;
}
bool GameplayWorld::removeRenderable(EntityId value) {
    return valid(value) && m_registry.remove<Renderable>(entity(value))>0;
}
bool GameplayWorld::setMaterial(EntityId value,std::optional<Renderable::Material> material) {
    if(!valid(value))return false;
    auto* renderable=m_registry.try_get<Renderable>(entity(value));if(!renderable)return false;
    renderable->material=std::move(material);return true;
}
bool GameplayWorld::setMeshVertices(EntityId value,std::vector<Renderable::MeshVertexEdit> vertices) {
    if(!valid(value))return false;
    auto* renderable=m_registry.try_get<Renderable>(entity(value));if(!renderable)return false;
    renderable->meshVertices=std::move(vertices);return true;
}
bool GameplayWorld::setParticleKinematic(EntityId value,bool enabled) {
    if(!valid(value))return false;
    auto* renderable=m_registry.try_get<Renderable>(entity(value));if(!renderable)return false;
    renderable->particleKinematic=enabled;return true;
}
bool GameplayWorld::setLight(EntityId value,std::optional<Light> light) {
    if(!valid(value))return false;
    if(light)m_registry.emplace_or_replace<Light>(entity(value),*light);else m_registry.remove<Light>(entity(value));
    return true;
}
bool GameplayWorld::setProbe(EntityId value,std::optional<ReflectionProbe> probe) {
    if(!valid(value))return false;
    if(probe)m_registry.emplace_or_replace<ReflectionProbe>(entity(value),*probe);else m_registry.remove<ReflectionProbe>(entity(value));
    return true;
}
bool GameplayWorld::setParticleSystem(EntityId value,std::optional<ParticleSystem> particles) {
    if(!valid(value))return false;
    if(particles)m_registry.emplace_or_replace<ParticleSystem>(entity(value),*particles);
    else m_registry.remove<ParticleSystem>(entity(value));
    return true;
}
void GameplayWorld::copyComponents(EntityId value,const EntitySnapshot& source) {
    if(!valid(value))return;
    if(source.renderable)m_registry.emplace_or_replace<Renderable>(entity(value),*source.renderable);
    else removeRenderable(value);
    setLight(value,source.light);setProbe(value,source.probe);setParticleSystem(value,source.particles);
}

bool GameplayWorld::setVisible(EntityId value, bool visible) {
    const auto candidate = entity(value);
    if (!m_registry.valid(candidate)) return false;
    auto* renderable = m_registry.try_get<Renderable>(candidate);
    if (!renderable) return false;
    renderable->visible = visible;
    return true;
}

std::vector<EntitySnapshot> GameplayWorld::snapshots() const {
    std::vector<EntitySnapshot> result;
    result.reserve(size());
    auto view = m_registry.view<Name, Transform, Tags>();
    for (const auto value : view) {
        EntitySnapshot item;
        const bool found = snapshot(id(value), item);
        if (!found) continue;
        result.push_back(std::move(item));
    }
    std::sort(result.begin(), result.end(), [](const auto& a, const auto& b) {
        return a.id < b.id;
    });
    return result;
}

} // namespace genesis::gameplay
