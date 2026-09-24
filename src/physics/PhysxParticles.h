#pragma once
#include "GameplayWorld.h"
#include <array>
#include <memory>
#include <string>
#include <vector>

namespace genesis::physics {
struct ParticleFrame {
    gameplay::EntityId entity{};
    gameplay::ParticleSystem::Mode mode{};
    float radius=.05f;
    std::array<float,3> origin{};
    float age=0;
    float effectDuration=0;
    std::vector<gameplay::ParticleSystem::Layer> layers; // Visual emitter appearance.
    std::vector<float> progress; // Normalized age for each visual particle.
    std::vector<std::array<float,3>> positions;
    std::vector<std::array<unsigned,3>> triangles; // Cloth only.
};
struct ColliderMesh {
    gameplay::EntityId entity{};
    gameplay::Transform initialTransform{};
    bool kinematic=false;
    std::vector<std::array<float,3>> positions; // World space, matching rendered geometry.
    std::vector<std::array<unsigned,3>> triangles;
};
std::vector<ParticleFrame> particleSeedFrames(const std::vector<gameplay::EntitySnapshot>& entities);

// A Play-owned PhysX scene. All positions are in world space and are read back
// after each fixed simulation step. Construct once per Play session.
class PhysxParticles {
public:
    PhysxParticles();
    ~PhysxParticles();
    PhysxParticles(const PhysxParticles&)=delete;
    PhysxParticles& operator=(const PhysxParticles&)=delete;
    bool start(const std::vector<gameplay::EntitySnapshot>& entities,
        const std::vector<ColliderMesh>& colliders,std::string& error);
    bool step(float seconds,std::string& error);
    void syncTransforms(const std::vector<gameplay::EntitySnapshot>& entities);
    void stop();
    const std::vector<ParticleFrame>& frames() const;
private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};
}
