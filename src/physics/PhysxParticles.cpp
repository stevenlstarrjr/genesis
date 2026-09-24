#include "physics/PhysxParticles.h"
#include <PxPhysicsAPI.h>
#include <extensions/PxCudaHelpersExt.h>
#include <extensions/PxDeformableSurfaceExt.h>
#include <extensions/PxParticleExt.h>
#include <cooking/PxCooking.h>
#include <bx/math.h>
#include <algorithm>

namespace genesis::physics {
using namespace physx;

static PxTransform rigidPose(const gameplay::Transform& transform){
    float matrix[16];
    constexpr float degreesToRadians=0.017453292519943295f;
    bx::mtxSRT(matrix,1,1,1,transform.rotation[0]*degreesToRadians,
        transform.rotation[1]*degreesToRadians,transform.rotation[2]*degreesToRadians,0,0,0);
    const PxMat33 rotation(PxVec3(matrix[0],matrix[1],matrix[2]),
        PxVec3(matrix[4],matrix[5],matrix[6]),PxVec3(matrix[8],matrix[9],matrix[10]));
    return PxTransform(PxVec3(transform.position[0],transform.position[1],transform.position[2]),PxQuat(rotation));
}

static void updateEmitter(ParticleFrame& frame,float age){
    frame.age=age;
    auto fraction=[](float value){return value-std::floor(value);};
    size_t index=0;
    for(size_t layerIndex=0;layerIndex<frame.layers.size();++layerIndex){
        const auto& layer=frame.layers[layerIndex];
        for(int i=0;i<layer.count;++i,++index){
            const float seed=fraction(float(i+1+layerIndex*71)*.61803399f);
            const float phase=fraction(age/layer.lifetime+fraction(float(i+1+layerIndex*19)*.75487767f));
            const float time=phase*layer.lifetime;
            const float angle=float(i+layerIndex*41)*2.39996323f;
            const float radius=std::max(0.f,layer.spawnRadius*std::sqrt(seed)+layer.radialSpeed*time);
            const float drift=std::sin(age*2.1f+float(i)*.73f)*layer.turbulence*phase;
            auto& p=frame.positions[index];frame.progress[index]=phase;
            p[0]=frame.origin[0]+layer.offset[0]+layer.velocity[0]*time+.5f*layer.acceleration[0]*time*time
                +std::cos(angle)*radius+drift;
            p[1]=frame.origin[1]+layer.offset[1]+layer.velocity[1]*time+.5f*layer.acceleration[1]*time*time;
            p[2]=frame.origin[2]+layer.offset[2]+layer.velocity[2]*time+.5f*layer.acceleration[2]*time*time
                +std::sin(angle)*radius;
        }
    }
}

std::vector<ParticleFrame> particleSeedFrames(const std::vector<gameplay::EntitySnapshot>& entities){
    std::vector<ParticleFrame> frames;
    for(const auto& entity:entities)if(entity.particles && entity.particles->enabled){
        const auto& settings=*entity.particles;const auto& dimensions=settings.dimensions;
        if(settings.mode!=gameplay::ParticleSystem::Mode::Emitter){
            if(std::any_of(dimensions.begin(),dimensions.end(),[](int d){return d<1 || d>128;}))continue;
            if(int64_t(dimensions[0])*dimensions[1]*dimensions[2]>65536)continue;
        }
        ParticleFrame frame;frame.entity=entity.id;frame.mode=settings.mode;frame.radius=settings.spacing*0.48f;
        frame.origin=entity.transform.position;frame.effectDuration=settings.mode==gameplay::ParticleSystem::Mode::Explosion?settings.effectDuration:0.f;
        if(settings.mode==gameplay::ParticleSystem::Mode::Emitter){
            size_t count=0;bool valid=true;for(const auto& layer:settings.layers){
                if(layer.count<1 || layer.count>4096 || layer.lifetime<=0){valid=false;continue;}
                count+=size_t(layer.count);
            }
            if(!valid || settings.layers.empty() || settings.layers.size()>8 || count>4096)continue;
            frame.layers=settings.layers;frame.positions.resize(count);frame.progress.resize(count);
            updateEmitter(frame,0.f);frames.push_back(std::move(frame));continue;
        }
        if(settings.mode==gameplay::ParticleSystem::Mode::Explosion){frame.positions.push_back(frame.origin);frames.push_back(std::move(frame));continue;}
        for(int x=0;x<dimensions[0];++x)for(int y=0;y<dimensions[1];++y)for(int z=0;z<dimensions[2];++z){
            std::array<float,3> p{};
            for(int axis=0;axis<3;++axis){const int coordinate=axis==0?x:axis==1?y:z;
                p[axis]=entity.transform.position[axis]+(coordinate-(dimensions[axis]-1)*0.5f)*settings.spacing*entity.transform.scale[axis];}
            frame.positions.push_back(p);
        }
        if(settings.mode==gameplay::ParticleSystem::Mode::Cloth && dimensions[1]==1){
            const int nx=dimensions[0],nz=dimensions[2];
            for(int x=1;x<nx;++x)for(int z=1;z<nz;++z){
                const auto a=unsigned((x-1)*nz+z-1),b=unsigned(x*nz+z-1),c=unsigned((x-1)*nz+z),d=unsigned(x*nz+z);
                frame.triangles.push_back({a,b,c});frame.triangles.push_back({c,b,d});
            }
        }
        frames.push_back(std::move(frame));
    }
    return frames;
}

struct PhysxParticles::Impl {
    PxDefaultAllocator allocator;
    PxDefaultErrorCallback errors;
    PxFoundation* foundation{};
    PxPhysics* physics{};
    PxCudaContextManager* cuda{};
    PxDefaultCpuDispatcher* dispatcher{};
    PxScene* scene{};
    PxMaterial* groundMaterial{};
    PxRigidStatic* groundPlane{};
    struct Collider {gameplay::EntityId entity;gameplay::Transform initialTransform;PxRigidActor* actor;PxRigidDynamic* moving;};
    std::vector<Collider> colliders;
    std::vector<PxTriangleMesh*> colliderMeshes;
    struct System {
        PxPBDParticleSystem* particles{};
        PxParticleBuffer* buffer{};
        PxPBDMaterial* particleMaterial{};
        PxDeformableSurface* cloth{};
        PxDeformableAttachment* attachment{};
        gameplay::EntityId entity{};
        gameplay::Transform initialTransform{};
        float age=0;
        float effectDuration=0;
        float blastRadius=0;
        float blastImpulse=0;
        bool burstApplied=false;
        PxDeformableSurfaceMaterial* clothMaterial{};
        PxTriangleMesh* mesh{};
    };
    std::vector<System> systems;
    std::vector<ParticleFrame> frames;

    void stop() {
        for(auto& system:systems)if(system.attachment)system.attachment->release();
        for(auto& collider:colliders)collider.actor->release();colliders.clear();
        for(auto* mesh:colliderMeshes)mesh->release();colliderMeshes.clear();
        if(groundPlane){groundPlane->release();groundPlane=nullptr;}
        for(auto& system:systems) {
            if(system.particles && system.buffer) system.particles->removeParticleBuffer(system.buffer);
            if(system.buffer)system.buffer->release();
            if(system.particles)system.particles->release();
            if(system.cloth)system.cloth->release();
            if(system.mesh)system.mesh->release();
            if(system.particleMaterial)system.particleMaterial->release();
            if(system.clothMaterial)system.clothMaterial->release();
        }
        systems.clear();frames.clear();
        if(scene){scene->release();scene=nullptr;}
        if(groundMaterial){groundMaterial->release();groundMaterial=nullptr;}
        if(dispatcher){dispatcher->release();dispatcher=nullptr;}
        if(physics){physics->release();physics=nullptr;}
        if(cuda){cuda->release();cuda=nullptr;}
        if(foundation){foundation->release();foundation=nullptr;}
    }

    bool addParticles(const gameplay::EntitySnapshot& entity,std::string& error) {
        const auto& settings=*entity.particles;
        const auto count=unsigned(settings.dimensions[0]*settings.dimensions[1]*settings.dimensions[2]);
        if(!count || count>65536){error="Particle count must be between 1 and 65,536";return false;}
        System system;
        system.entity=entity.id;system.initialTransform=entity.transform;
        if(settings.mode==gameplay::ParticleSystem::Mode::Explosion){
            system.effectDuration=settings.effectDuration;system.blastRadius=settings.blastRadius;
            system.blastImpulse=settings.blastImpulse;
        }
        system.particleMaterial=physics->createPBDMaterial(settings.friction,settings.friction,0.f,
            settings.damping,0.5f,settings.viscosity,settings.cohesion,0.f,0.f);
        if(!system.particleMaterial){error="PhysX could not create a particle material";return false;}
        system.particles=physics->createPBDParticleSystem(*cuda,96);
        if(!system.particles){system.particleMaterial->release();error="PhysX could not create a PBD particle system";return false;}
        // Solid particles settle at twice their solid rest offset. Keep that
        // diameter below the authored grid spacing so a granular block does
        // not start deeply overlapped and eject itself on the first step.
        const float minScale=std::min({std::abs(entity.transform.scale[0]),
            std::abs(entity.transform.scale[1]),std::abs(entity.transform.scale[2])});
        const float rest=settings.mode==gameplay::ParticleSystem::Mode::Granular
            ?settings.spacing*std::max(minScale,0.001f)*0.48f
            :settings.spacing*0.5f/0.6f;
        system.particles->setRestOffset(rest);
        system.particles->setContactOffset(rest+0.01f);
        system.particles->setParticleContactOffset(rest+0.01f);
        system.particles->setSolidRestOffset(rest);
        system.particles->setFluidRestOffset(rest*0.6f);
        scene->addActor(*system.particles);
        const auto flags=settings.mode==gameplay::ParticleSystem::Mode::Fluid
            ?PxParticlePhaseFlags(PxParticlePhaseFlag::eParticlePhaseFluid|PxParticlePhaseFlag::eParticlePhaseSelfCollide)
            :PxParticlePhaseFlags(PxParticlePhaseFlag::eParticlePhaseSelfCollide);
        const PxU32 phase=system.particles->createPhase(system.particleMaterial,flags);
        auto* phases=PX_EXT_PINNED_MEMORY_ALLOC(PxU32,*cuda,count);
        auto* positions=PX_EXT_PINNED_MEMORY_ALLOC(PxVec4,*cuda,count);
        auto* velocities=PX_EXT_PINNED_MEMORY_ALLOC(PxVec4,*cuda,count);
        ParticleFrame frame;frame.entity=entity.id;frame.mode=settings.mode;frame.radius=settings.spacing*0.48f;
        frame.origin=entity.transform.position;frame.effectDuration=system.effectDuration;frame.positions.reserve(count);
        const auto& dimensions=settings.dimensions;
        for(int x=0;x<dimensions[0];++x)for(int y=0;y<dimensions[1];++y)for(int z=0;z<dimensions[2];++z){
            const unsigned i=unsigned((x*dimensions[1]+y)*dimensions[2]+z);
            std::array<float,3> p{};
            PxVec3 velocity(0.f);
            if(settings.mode==gameplay::ParticleSystem::Mode::Explosion){
                const float vertical=1.f-2.f*(float(i)+.5f)/float(count);
                const float angle=float(i)*2.39996323f;
                const float horizontal=std::sqrt(std::max(0.f,1.f-vertical*vertical));
                const PxVec3 direction(std::cos(angle)*horizontal,vertical,std::sin(angle)*horizontal);
                const float scatter=float((i*37u)%101u)/101.f;
                const float offset=.04f+.22f*scatter;
                for(int axis=0;axis<3;++axis)p[axis]=frame.origin[axis]+direction[axis]*offset;
                velocity=direction*(settings.burstSpeed*(.65f+.6f*scatter));
                velocity.y+=settings.burstSpeed*.15f;
            }else for(int axis=0;axis<3;++axis){
                const int coordinate=axis==0?x:axis==1?y:z;
                p[axis]=entity.transform.position[axis]+(coordinate-(dimensions[axis]-1)*0.5f)*settings.spacing*entity.transform.scale[axis];
            }
            phases[i]=phase;positions[i]=PxVec4(p[0],p[1],p[2],1.f/std::max(settings.mass,0.0001f));
            velocities[i]=PxVec4(velocity,0.f);frame.positions.push_back(p);
        }
        ExtGpu::PxParticleBufferDesc desc;
        desc.maxParticles=count;desc.numActiveParticles=count;
        desc.positions=positions;desc.velocities=velocities;desc.phases=phases;
        system.buffer=ExtGpu::PxCreateAndPopulateParticleBuffer(desc,cuda);
        PX_EXT_PINNED_MEMORY_FREE(*cuda,positions);
        PX_EXT_PINNED_MEMORY_FREE(*cuda,velocities);
        PX_EXT_PINNED_MEMORY_FREE(*cuda,phases);
        if(!system.buffer){system.particles->release();system.particleMaterial->release();error="PhysX could not upload particle positions";return false;}
        system.particles->addParticleBuffer(system.buffer);
        systems.push_back(system);frames.push_back(std::move(frame));return true;
    }

    bool addCloth(const gameplay::EntitySnapshot& entity,std::string& error) {
        const auto& settings=*entity.particles;
        const int nx=settings.dimensions[0],nz=settings.dimensions[2];
        if(nx<2 || nz<2 || settings.dimensions[1]!=1){error="Cloth needs at least 2 by 2 vertices and one layer";return false;}
        System system;
        system.entity=entity.id;system.initialTransform=entity.transform;
        std::vector<PxVec3> vertices;std::vector<PxU32> triangles;
        ParticleFrame frame;frame.entity=entity.id;frame.mode=settings.mode;frame.radius=settings.spacing*0.48f;
        for(int x=0;x<nx;++x)for(int z=0;z<nz;++z){
            const float px=(x-(nx-1)*0.5f)*settings.spacing*entity.transform.scale[0];
            const float pz=(z-(nz-1)*0.5f)*settings.spacing*entity.transform.scale[2];
            vertices.emplace_back(px,0.f,pz);
            frame.positions.push_back({px+entity.transform.position[0],entity.transform.position[1],pz+entity.transform.position[2]});
        }
        for(int x=1;x<nx;++x)for(int z=1;z<nz;++z){
            const auto a=unsigned((x-1)*nz+z-1),b=unsigned(x*nz+z-1),c=unsigned((x-1)*nz+z),d=unsigned(x*nz+z);
            triangles.insert(triangles.end(),{a,b,c,c,b,d});
            frame.triangles.push_back({a,b,c});frame.triangles.push_back({c,b,d});
        }
        PxTriangleMeshDesc desc;
        desc.points.count=PxU32(vertices.size());desc.points.stride=sizeof(PxVec3);desc.points.data=vertices.data();
        desc.triangles.count=PxU32(triangles.size()/3);desc.triangles.stride=3*sizeof(PxU32);desc.triangles.data=triangles.data();
        PxCookingParams cooking(physics->getTolerancesScale());cooking.buildGPUData=true;
        system.mesh=PxCreateTriangleMesh(cooking,desc,physics->getPhysicsInsertionCallback());
        if(!system.mesh){error="PhysX could not cook the cloth mesh";return false;}
        const float thickness=std::max(settings.spacing*0.1f,0.001f);
        system.clothMaterial=physics->createDeformableSurfaceMaterial(settings.stiffness,0.3f,settings.friction,thickness,settings.stiffness*0.001f);
        if(!system.clothMaterial){system.mesh->release();error="PhysX could not create a cloth material";return false;}
        system.cloth=physics->createDeformableSurface(*cuda);
        if(!system.cloth){system.clothMaterial->release();system.mesh->release();error="PhysX could not create a deformable surface";return false;}
        PxTriangleMeshGeometry geometry(system.mesh);
        PxDeformableSurfaceMaterial* materials[]{system.clothMaterial};
        PxShape* shape=physics->createShape(geometry,materials,1,true,PxShapeFlag::eSIMULATION_SHAPE|PxShapeFlag::eSCENE_QUERY_SHAPE|PxShapeFlag::eVISUALIZATION);
        if(!shape){system.cloth->release();system.clothMaterial->release();system.mesh->release();error="PhysX could not create a cloth shape";return false;}
        system.cloth->attachShape(*shape);shape->release();
        scene->addActor(*system.cloth);
        PxVec4 *positions{},*velocities{},*rest{};
        const auto transform=PxTransform(PxVec3(entity.transform.position[0],entity.transform.position[1],entity.transform.position[2]));
        PxDeformableSurfaceExt::allocateAndInitializeHostMirror(*system.cloth,vertices.data(),nullptr,vertices.data(),
            std::max(settings.mass,0.0001f),transform,cuda,positions,velocities,rest);
        system.cloth->getShape()->setContactOffset(thickness*2.f);
        system.cloth->getShape()->setRestOffset(thickness);
        system.cloth->setLinearDamping(settings.damping);
        PxDeformableSurfaceExt::copyToDevice(*system.cloth,PxDeformableSurfaceDataFlag::eALL,PxU32(vertices.size()),positions,velocities,rest);
        PX_EXT_PINNED_MEMORY_FREE(*cuda,positions);PX_EXT_PINNED_MEMORY_FREE(*cuda,velocities);PX_EXT_PINNED_MEMORY_FREE(*cuda,rest);
        systems.push_back(system);frames.push_back(std::move(frame));
        if(settings.pinEdge!=gameplay::ParticleSystem::PinEdge::None){
            std::vector<PxU32> indices;std::vector<PxVec4> targets;
            for(int x=0;x<nx;++x){
                const bool pin=(x==0 && (settings.pinEdge==gameplay::ParticleSystem::PinEdge::Left || settings.pinEdge==gameplay::ParticleSystem::PinEdge::Both))
                    || (x==nx-1 && (settings.pinEdge==gameplay::ParticleSystem::PinEdge::Right || settings.pinEdge==gameplay::ParticleSystem::PinEdge::Both));
                if(!pin)continue;
                for(int z=0;z<nz;++z){const auto index=unsigned(x*nz+z);const auto& p=frames.back().positions[index];
                    indices.push_back(index);targets.emplace_back(p[0],p[1],p[2],0.f);
                }
            }
            PxDeformableAttachmentData attachment;
            attachment.actor[0]=system.cloth;attachment.type[0]=PxDeformableAttachmentTargetType::eVERTEX;
            attachment.indices[0].data=indices.data();attachment.indices[0].count=PxU32(indices.size());
            attachment.actor[1]=nullptr;attachment.type[1]=PxDeformableAttachmentTargetType::eWORLD;
            attachment.coords[1].data=targets.data();attachment.coords[1].count=PxU32(targets.size());
            systems.back().attachment=physics->createDeformableAttachment(attachment);
            if(!systems.back().attachment){error="PhysX could not pin the cloth edge";return false;}
        }
        return true;
    }
    bool addCollider(const ColliderMesh& collider,std::string& error){
        if(collider.positions.empty() || collider.triangles.empty())return true;
        std::vector<PxVec3> vertices;vertices.reserve(collider.positions.size());
        for(const auto& position:collider.positions)vertices.emplace_back(position[0],position[1],position[2]);
        std::vector<PxU32> indices;indices.reserve(collider.triangles.size()*3);
        for(const auto& triangle:collider.triangles){
            if(triangle[0]>=vertices.size() || triangle[1]>=vertices.size() || triangle[2]>=vertices.size()){
                error="Authored mesh has an invalid collision triangle";return false;
            }
            indices.insert(indices.end(),{triangle[0],triangle[1],triangle[2]});
        }
        PxTriangleMeshDesc desc;
        desc.points.count=PxU32(vertices.size());desc.points.stride=sizeof(PxVec3);desc.points.data=vertices.data();
        desc.triangles.count=PxU32(collider.triangles.size());desc.triangles.stride=3*sizeof(PxU32);desc.triangles.data=indices.data();
        PxCookingParams cooking(physics->getTolerancesScale());cooking.buildGPUData=true;
        auto* mesh=PxCreateTriangleMesh(cooking,desc,physics->getPhysicsInsertionCallback());
        if(!mesh){error="PhysX could not cook an authored collision mesh";return false;}
        PxRigidActor* actor=collider.kinematic
            ?static_cast<PxRigidActor*>(physics->createRigidDynamic(PxTransform(PxIdentity)))
            :static_cast<PxRigidActor*>(physics->createRigidStatic(PxTransform(PxIdentity)));
        if(!actor){mesh->release();error="PhysX could not create a mesh collider";return false;}
        PxRigidDynamic* moving=collider.kinematic?static_cast<PxRigidDynamic*>(actor):nullptr;
        if(moving)moving->setRigidBodyFlag(PxRigidBodyFlag::eKINEMATIC,true);
        PxTriangleMeshGeometry geometry(mesh,PxMeshScale(),PxMeshGeometryFlag::eDOUBLE_SIDED);
        auto* shape=physics->createShape(geometry,*groundMaterial);
        if(!shape){actor->release();mesh->release();error="PhysX could not create a mesh collider shape";return false;}
        actor->attachShape(*shape);shape->release();scene->addActor(*actor);
        colliderMeshes.push_back(mesh);colliders.push_back({collider.entity,collider.initialTransform,actor,moving});return true;
    }
};

PhysxParticles::PhysxParticles():m_impl(std::make_unique<Impl>()){}
PhysxParticles::~PhysxParticles(){stop();}
void PhysxParticles::stop(){m_impl->stop();}
const std::vector<ParticleFrame>& PhysxParticles::frames() const{return m_impl->frames;}
bool PhysxParticles::start(const std::vector<gameplay::EntitySnapshot>& entities,
    const std::vector<ColliderMesh>& colliders,std::string& error){
    stop();auto& s=*m_impl;
    const bool any=std::any_of(entities.begin(),entities.end(),[](const auto& e){return e.particles && e.particles->enabled;});
    if(!any)return true;
    const bool needsPhysx=std::any_of(entities.begin(),entities.end(),[](const auto& e){
        return e.particles && e.particles->enabled && e.particles->mode!=gameplay::ParticleSystem::Mode::Emitter;
    });
    if(!needsPhysx){
        s.frames=particleSeedFrames(entities);
        const auto expected=std::count_if(entities.begin(),entities.end(),[](const auto& e){
            return e.particles && e.particles->enabled;
        });
        if(s.frames.size()!=size_t(expected)){error="Invalid visual emitter layers";stop();return false;}
        for(const auto& frame:s.frames){Impl::System fire;fire.entity=frame.entity;s.systems.push_back(fire);}
        return true;
    }
    s.foundation=PxCreateFoundation(PX_PHYSICS_VERSION,s.allocator,s.errors);
    if(!s.foundation){error="PhysX foundation initialization failed";return false;}
    PxCudaContextManagerDesc cudaDesc;
    s.cuda=PxCreateCudaContextManager(*s.foundation,cudaDesc,PxGetProfilerCallback());
    if(!s.cuda || !s.cuda->contextIsValid()){error="NVIDIA CUDA context is unavailable";stop();return false;}
    s.physics=PxCreatePhysics(PX_PHYSICS_VERSION,*s.foundation,PxTolerancesScale(),true);
    if(!s.physics){error="PhysX initialization failed";stop();return false;}
    PxSceneDesc sceneDesc(s.physics->getTolerancesScale());
    sceneDesc.gravity=PxVec3(0.f,-9.81f,0.f);sceneDesc.cudaContextManager=s.cuda;
    s.dispatcher=PxDefaultCpuDispatcherCreate(2);sceneDesc.cpuDispatcher=s.dispatcher;
    sceneDesc.filterShader=PxDefaultSimulationFilterShader;
    sceneDesc.flags|=PxSceneFlag::eENABLE_GPU_DYNAMICS;sceneDesc.flags|=PxSceneFlag::eENABLE_PCM;
    sceneDesc.broadPhaseType=PxBroadPhaseType::eGPU;sceneDesc.solverType=PxSolverType::eTGS;
    s.scene=s.physics->createScene(sceneDesc);
    if(!s.scene){error="PhysX GPU scene creation failed";stop();return false;}
    s.groundMaterial=s.physics->createMaterial(.5f,.5f,.5f);
    s.groundPlane=PxCreatePlane(*s.physics,PxPlane(0.f,1.f,0.f,0.f),*s.groundMaterial);
    s.scene->addActor(*s.groundPlane);
    for(const auto& collider:colliders)if(!s.addCollider(collider,error)){stop();return false;}
    for(const auto& entity:entities)if(entity.particles && entity.particles->enabled){
        if(entity.particles->mode==gameplay::ParticleSystem::Mode::Emitter){
            Impl::System fire;fire.entity=entity.id;fire.initialTransform=entity.transform;
            s.systems.push_back(fire);
            auto frames=particleSeedFrames({entity});
            if(frames.empty()){error="Invalid visual emitter layers";stop();return false;}
            s.frames.push_back(std::move(frames.front()));
            continue;
        }
        const bool okay=entity.particles->mode==gameplay::ParticleSystem::Mode::Cloth
            ?s.addCloth(entity,error):s.addParticles(entity,error);
        if(!okay){stop();return false;}
    }
    return true;
}
void PhysxParticles::syncTransforms(const std::vector<gameplay::EntitySnapshot>& entities){
    auto& s=*m_impl;
    const auto find=[&](gameplay::EntityId id)->const gameplay::EntitySnapshot*{
        const auto it=std::find_if(entities.begin(),entities.end(),[&](const auto& entity){return entity.id==id;});
        return it==entities.end()?nullptr:&*it;
    };
    for(auto& collider:s.colliders)if(collider.moving)
        if(const auto* entity=find(collider.entity))
            collider.moving->setKinematicTarget(rigidPose(entity->transform)*rigidPose(collider.initialTransform).getInverse());
    for(auto& system:s.systems)if(system.attachment)
        if(const auto* entity=find(system.entity))
            system.attachment->updatePose(rigidPose(entity->transform)*rigidPose(system.initialTransform).getInverse());
    for(size_t i=0;i<s.systems.size();++i)if(s.frames[i].mode==gameplay::ParticleSystem::Mode::Emitter)
        if(const auto* entity=find(s.systems[i].entity))s.frames[i].origin=entity->transform.position;
}
bool PhysxParticles::step(float seconds,std::string& error){
    auto& s=*m_impl;
    for(auto& frame:s.frames)if(frame.mode==gameplay::ParticleSystem::Mode::Emitter)
        updateEmitter(frame,frame.age+seconds);
    if(!s.scene)return true;
    for(auto& blast:s.systems)if(blast.particles && blast.blastRadius>0 && !blast.burstApplied){
        blast.burstApplied=true;
        const size_t blastIndex=size_t(&blast-s.systems.data());
        const auto& origin=s.frames[blastIndex].origin;
        s.cuda->acquireContext();
        for(size_t clothIndex=0;clothIndex<s.systems.size();++clothIndex){
            auto& cloth=s.systems[clothIndex];if(!cloth.cloth)continue;
            const auto& positions=s.frames[clothIndex].positions;
            std::vector<PxVec4> velocities(positions.size());
            auto* device=cloth.cloth->getVelocityBufferD();
            s.cuda->getCudaContext()->memcpyDtoH(velocities.data(),reinterpret_cast<CUdeviceptr>(device),velocities.size()*sizeof(PxVec4));
            bool affected=false;
            for(size_t i=0;i<positions.size();++i){const auto& p=positions[i];
                PxVec3 delta(p[0]-origin[0],p[1]-origin[1],p[2]-origin[2]);
                const float distance=delta.magnitude();if(distance>=blast.blastRadius)continue;
                if(distance<.05f)delta=PxVec3(0,1,0);else delta/=distance;
                const float falloff=1.f-distance/blast.blastRadius;
                const PxVec3 impulse=delta*(blast.blastImpulse*falloff*falloff);
                velocities[i].x+=impulse.x;velocities[i].y+=impulse.y;velocities[i].z+=impulse.z;
                affected=true;
            }
            if(affected){
                s.cuda->getCudaContext()->memcpyHtoD(reinterpret_cast<CUdeviceptr>(device),velocities.data(),velocities.size()*sizeof(PxVec4));
                cloth.cloth->markDirty(PxDeformableSurfaceDataFlag::eVELOCITY);
            }
        }
        s.cuda->releaseContext();
    }
    s.scene->simulate(seconds);if(!s.scene->fetchResults(true)){error="PhysX simulation step failed";return false;}
    s.cuda->acquireContext();
    std::vector<size_t> expired;
    for(size_t i=0;i<s.systems.size();++i){
        auto& system=s.systems[i];auto& frame=s.frames[i];
        if(frame.mode==gameplay::ParticleSystem::Mode::Emitter)continue;
        if(system.particles && !system.buffer)continue;
        std::vector<PxVec4> positions(frame.positions.size());
        const auto source=system.buffer?system.buffer->getPositionInvMasses():system.cloth->getPositionInvMassBufferD();
        s.cuda->getCudaContext()->memcpyDtoH(positions.data(),reinterpret_cast<CUdeviceptr>(source),positions.size()*sizeof(PxVec4));
        for(size_t j=0;j<positions.size();++j)frame.positions[j]={positions[j].x,positions[j].y,positions[j].z};
        if(system.effectDuration>0){
            frame.age=system.age=std::min(system.age+seconds,system.effectDuration);
            if(system.age>=system.effectDuration)expired.push_back(i);
        }
    }
    s.cuda->releaseContext();
    for(const size_t i:expired){auto& system=s.systems[i];
        system.particles->removeParticleBuffer(system.buffer);
        system.buffer->release();system.buffer=nullptr;s.frames[i].positions.clear();
    }
    return true;
}
}
