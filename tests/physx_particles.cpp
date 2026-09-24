#include "physics/PhysxParticles.h"
#include <algorithm>
#include <cmath>
#include <iostream>

int main(){
    using namespace genesis;
    std::vector<gameplay::EntitySnapshot> entities;
    for(auto mode:{gameplay::ParticleSystem::Mode::Fluid,gameplay::ParticleSystem::Mode::Granular,gameplay::ParticleSystem::Mode::Cloth}){
        gameplay::EntitySnapshot entity;entity.id=gameplay::EntityId(entities.size()+1);
        entity.transform.position={float(entities.size()*2),3.f,0.f};
        entity.particles=gameplay::ParticleSystem{};entity.particles->mode=mode;
        if(mode==gameplay::ParticleSystem::Mode::Cloth)entity.particles->pinEdge=gameplay::ParticleSystem::PinEdge::Left;
        entity.particles->dimensions=mode==gameplay::ParticleSystem::Mode::Cloth?std::array<int,3>{4,1,4}:std::array<int,3>{4,4,4};
        if(mode==gameplay::ParticleSystem::Mode::Granular){
            entity.particles->dimensions={6,4,6};entity.particles->spacing=.12f;
            entity.particles->friction=.8f;entity.particles->damping=.18f;
        }
        entities.push_back(entity);
    }
    physics::PhysxParticles simulation;std::string error;
    physics::ColliderMesh platform;
    platform.positions={{{-10,1.8f,-10}},{{10,1.8f,-10}},{{10,1.8f,10}},{{-10,1.8f,10}},
                        {{-10,2.f,-10}},{{10,2.f,-10}},{{10,2.f,10}},{{-10,2.f,10}}};
    platform.triangles={{{0,1,2}},{{0,2,3}},{{4,6,5}},{{4,7,6}},{{0,4,5}},{{0,5,1}},
                        {{1,5,6}},{{1,6,2}},{{2,6,7}},{{2,7,3}},{{3,7,4}},{{3,4,0}}};
    platform.entity=100;platform.kinematic=true;
    gameplay::EntitySnapshot platformEntity;platformEntity.id=100;entities.push_back(platformEntity);
    if(!simulation.start(entities,{platform},error)){std::cerr<<error<<'\n';return 1;}
    if(simulation.frames().size()!=3 || simulation.frames()[2].triangles.size()!=18){std::cerr<<"Incorrect PhysX frames\n";return 2;}
    const float initial=simulation.frames()[0].positions[0][1];
    const auto granularSeed=simulation.frames()[1].positions;
    if(!simulation.step(1.f/60.f,error)){std::cerr<<error<<'\n';return 3;}
    float maxGranularHorizontalStep=0.f;
    for(size_t i=0;i<granularSeed.size();++i){
        const auto& before=granularSeed[i];
        const auto& after=simulation.frames()[1].positions[i];
        maxGranularHorizontalStep=std::max(maxGranularHorizontalStep,
            std::hypot(after[0]-before[0],after[2]-before[2]));
    }
    if(maxGranularHorizontalStep>.03f){
        std::cerr<<"Granular seed burst sideways on first step: "<<maxGranularHorizontalStep<<'\n';return 20;
    }
    for(int i=1;i<70;++i)if(!simulation.step(1.f/60.f,error)){std::cerr<<error<<'\n';return 3;}
    if(!(simulation.frames()[0].positions[0][1]<initial-.05f)){std::cerr<<"Fluid did not fall under gravity\n";return 4;}
    float granularSeedHeight=0.f,granularSettledHeight=0.f;
    for(size_t i=0;i<granularSeed.size();++i){
        granularSeedHeight+=granularSeed[i][1];
        granularSettledHeight+=simulation.frames()[1].positions[i][1];
    }
    if(granularSettledHeight>granularSeedHeight-.05f*granularSeed.size()){
        std::cerr<<"Granular particles did not fall under gravity\n";return 21;
    }
    for(size_t i=0;i<simulation.frames().size();++i)
        if(!(simulation.frames()[i].positions[0][1]>1.8f)){
            std::cerr<<"Particle mode "<<i<<" passed through authored mesh collider\n";return 6;
        }
    const float pinnedBefore=simulation.frames()[2].positions[0][1];
    if(std::abs(pinnedBefore-3.f)>.08f){std::cerr<<"Cloth edge did not stay pinned\n";return 7;}
    float granularBefore=0;for(const auto& p:simulation.frames()[1].positions)granularBefore+=p[1];
    granularBefore/=simulation.frames()[1].positions.size();
    for(int i=1;i<=45;++i){
        entities[2].transform.position[1]=3.f+.5f*float(i)/45.f;
        entities[3].transform.position[1]=.7f*float(i)/45.f;
        simulation.syncTransforms(entities);
        if(!simulation.step(1.f/60.f,error)){std::cerr<<error<<'\n';return 8;}
    }
    if(std::abs(simulation.frames()[2].positions[0][1]-3.5f)>.12f){std::cerr<<"Cloth pin did not follow its entity: "<<simulation.frames()[2].positions[0][1]<<"\n";return 9;}
    float granularAfter=0;for(const auto& p:simulation.frames()[1].positions)granularAfter+=p[1];
    granularAfter/=simulation.frames()[1].positions.size();
    if(granularAfter<granularBefore+.15f){std::cerr<<"Moving mesh did not lift granular particles\n";return 10;}
    for(const auto& frame:simulation.frames())for(const auto& position:frame.positions)
        for(float value:position)if(!std::isfinite(value)){std::cerr<<"Non-finite position\n";return 5;}
    simulation.stop();
    gameplay::EntitySnapshot cloth;cloth.id=200;cloth.transform.position={0,3,0};
    cloth.particles=gameplay::ParticleSystem{};cloth.particles->mode=gameplay::ParticleSystem::Mode::Cloth;
    cloth.particles->dimensions={12,1,12};cloth.particles->spacing=.2f;
    cloth.particles->pinEdge=gameplay::ParticleSystem::PinEdge::Left;
    gameplay::EntitySnapshot explosion;explosion.id=201;explosion.transform.position={0,2,0};
    explosion.particles=gameplay::ParticleSystem{};explosion.particles->mode=gameplay::ParticleSystem::Mode::Explosion;
    explosion.particles->dimensions={6,6,6};explosion.particles->burstSpeed=8;
    explosion.particles->blastRadius=3;explosion.particles->blastImpulse=12;
    physics::PhysxParticles withBlast;
    if(!withBlast.start({cloth,explosion},{},error)){
        std::cerr<<"Explosion setup: "<<error<<'\n';return 11;
    }
    for(int i=0;i<12;++i)if(!withBlast.step(1.f/60.f,error)){
        std::cerr<<"Explosion step: "<<error<<'\n';return 12;
    }
    const auto freeVertex=size_t(6*12+6);
    const float blasted=withBlast.frames()[0].positions[freeVertex][1];
    if(std::abs(withBlast.frames()[0].positions[0][1]-3.f)>.1f){std::cerr<<"Explosion moved pinned cloth edge\n";return 14;}
    if(withBlast.frames()[1].age<=0 || withBlast.frames()[1].positions.size()!=216){
        std::cerr<<"Explosion particles did not advance\n";return 15;
    }
    for(int i=0;i<160;++i)if(!withBlast.step(1.f/60.f,error)){std::cerr<<"Explosion lifetime: "<<error<<'\n';return 18;}
    if(!withBlast.frames()[1].positions.empty() || withBlast.frames()[1].age<explosion.particles->effectDuration){
        std::cerr<<"Explosion particles outlived the effect\n";return 19;
    }
    withBlast.stop();
    physics::PhysxParticles withoutBlast;
    if(!withoutBlast.start({cloth},{},error)){std::cerr<<"Control setup: "<<error<<'\n';return 16;}
    for(int i=0;i<12;++i)if(!withoutBlast.step(1.f/60.f,error)){std::cerr<<"Control step: "<<error<<'\n';return 17;}
    const float control=withoutBlast.frames()[0].positions[freeVertex][1];
    if(blasted<control+.15f){std::cerr<<"Explosion did not push free cloth: "<<blasted<<" vs "<<control<<'\n';return 13;}
    gameplay::EntitySnapshot emitter;emitter.id=202;emitter.transform.position={2,0,0};
    emitter.particles=gameplay::ParticleSystem{};emitter.particles->mode=gameplay::ParticleSystem::Mode::Emitter;
    gameplay::ParticleSystem::Layer soft;soft.count=60;
    gameplay::ParticleSystem::Layer sparks;sparks.name="Sparks";sparks.count=36;
    sparks.shape=gameplay::ParticleSystem::Layer::Shape::Spark;sparks.velocity={0,1.5f,0};
    emitter.particles->layers={soft,sparks};
    physics::PhysxParticles visual;
    if(!visual.start({emitter},{},error) || visual.frames().size()!=1
        || visual.frames()[0].positions.size()!=96 || visual.frames()[0].layers.size()!=2){
        std::cerr<<"Emitter setup failed: "<<error<<'\n';return 22;
    }
    const auto first=visual.frames()[0].positions;
    for(int i=0;i<240;++i)if(!visual.step(1.f/60.f,error)){std::cerr<<"Emitter step: "<<error<<'\n';return 23;}
    if(visual.frames()[0].positions==first || visual.frames()[0].positions.size()!=first.size()){
        std::cerr<<"Emitter did not loop\n";return 24;
    }
    for(const auto& p:visual.frames()[0].positions)if(!std::isfinite(p[0]) || !std::isfinite(p[1])
        || !std::isfinite(p[2]) || p[1]<0 || p[1]>3.1f){std::cerr<<"Emitter escaped its plume\n";return 25;}
    emitter.transform.position={4,1,0};visual.syncTransforms({emitter});
    if(!visual.step(1.f/60.f,error) || visual.frames()[0].origin!=emitter.transform.position){
        std::cerr<<"Emitter did not follow its entity\n";return 26;
    }
    std::cout<<"PhysX particles collided with a moving mesh; cloth pins followed their entity\n";
    std::cout<<"Explosion burst pushed free cloth while the pinned edge held\n";
}
