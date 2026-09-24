#include "editor/GameEditor.h"
#include "ui/Theme.h"
#include "SceneComponents.h"
#include "ModelValidation.h"
#include <algorithm>
#include <cctype>

namespace genesis::editor {
using namespace ui;
namespace fs=std::filesystem;
namespace {
Node& row(Node& parent,const char* caption) {
    auto& result=parent.row();auto s=result.style();s.shrink=0;s.gap=5;s.align=Align::Center;result.setStyle(s);
    s=theme::label(9);s.width=86;result.label(caption).setStyle(s);return result;
}
Node& number(Node& parent,const char* caption,float minimum,float maximum,float step,std::function<void(float)> changed) {
    auto& mount=row(parent,caption);auto s=theme::field();s.grow=1;
    return mount.numberField(minimum,minimum,maximum,step,std::move(changed)).setStyle(s);
}
Node& button(Node& parent,const char* caption,std::function<void()> action) {return parent.button(caption,std::move(action)).setStyle(theme::button());}
Node& mount(Node& parent) {auto& node=parent.column();auto s=node.style();s.shrink=0;return node.setStyle(s);}
}
std::optional<gameplay::EntitySnapshot> GameEditor::selectedEntity() const {
    gameplay::EntitySnapshot entity;if(m_selected && m_world.snapshot(*m_selected,entity))return entity;return {};
}
void GameEditor::componentsChanged() {changed();rebuildTree();updateInspector();}
void GameEditor::setLightCapacity(std::array<int,3> capacity) {for(auto& n:capacity)n=std::max(n,0);m_lightCapacity=capacity;}
bool GameEditor::lightSlotAvailable(gameplay::Light::Type type,bool replacing) const {
    int used=0;for(const auto& entity:m_world.snapshots())if((!replacing || entity.id!=m_selected) && entity.light && entity.light->enabled && entity.light->type==type)++used;
    return used<m_lightCapacity[size_t(type)];
}
bool GameEditor::assignModel(const fs::path& path) {
    m_document.cancelInput();endTransformEdit(true);const auto entity=selectedEntity();if(!entity)return false;
    std::error_code ec;const auto resolved=fs::weakly_canonical(path,ec);auto ext=path.extension().string();for(auto& c:ext)c=char(std::tolower(static_cast<unsigned char>(c)));
    std::string error;
    if(ec || !fs::is_regular_file(resolved,ec) || (ext!=".glb" && ext!=".gltf")){status("Choose an existing .glb or .gltf model.");return false;}
    if(!validateModel(resolved,error)){status(error);return false;}
    if(entity->renderable && fs::path(entity->renderable->path)==resolved)return true;
    if(m_editorMode==EditorMode::Edit)setEditorMode(EditorMode::Object);
    checkpoint();m_world.setRenderable(entity->id,resolved.string());
    if(entity->renderable){m_world.setVisible(entity->id,entity->renderable->visible);m_world.setMaterial(entity->id,entity->renderable->material);m_world.setParticleKinematic(entity->id,entity->renderable->particleKinematic);}
    componentsChanged();status("Assigned "+resolved.filename().string()+" to "+entity->name);return true;
}
void GameEditor::removeMesh() {
    m_document.cancelInput();endTransformEdit(true);const auto entity=selectedEntity();if(!entity || !entity->renderable)return;
    if(m_editorMode==EditorMode::Edit)setEditorMode(EditorMode::Object);
    checkpoint();m_world.removeRenderable(entity->id);componentsChanged();status("Removed Mesh Renderer; object and transform retained.");
}
bool GameEditor::setMaterial(std::optional<gameplay::Renderable::Material> material) {
    auto entity=selectedEntity();if(!entity || !entity->renderable)return false;
    if(entity->renderable->material==material)return true;
    entity->renderable->material=material;if(!components::valid(*entity)){status("Invalid material value.");return false;}
    checkpoint();m_world.setMaterial(entity->id,material);componentsChanged();return true;
}
bool GameEditor::setParticleKinematic(bool enabled) {
    const auto entity=selectedEntity();if(!entity || !entity->renderable)return false;
    if(entity->renderable->particleKinematic==enabled)return true;
    checkpoint();m_world.setParticleKinematic(entity->id,enabled);componentsChanged();return true;
}
bool GameEditor::setLight(std::optional<gameplay::Light> light) {
    auto entity=selectedEntity();if(!entity)return false;if(entity->light==light)return true;
    entity->light=light;if(!components::valid(*entity)){status("Invalid light value or cone angles.");return false;}
    if(light && light->enabled && !lightSlotAvailable(light->type)){status("No available light slot of this type. Disable or remove another light.");updateInspector();return false;}
    checkpoint();m_world.setLight(entity->id,light);componentsChanged();return true;
}
bool GameEditor::setProbe(std::optional<gameplay::ReflectionProbe> probe) {
    auto entity=selectedEntity();if(!entity)return false;if(entity->probe==probe)return true;
    entity->probe=probe;if(!components::valid(*entity)){status("Invalid reflection probe value.");return false;}
    checkpoint();m_world.setProbe(entity->id,probe);componentsChanged();return true;
}
bool GameEditor::setParticles(std::optional<gameplay::ParticleSystem> particles) {
    auto entity=selectedEntity();if(!entity)return false;if(entity->particles==particles)return true;
    entity->particles=particles;if(!components::valid(*entity)){status("Invalid particle system settings or count.");updateInspector();return false;}
    checkpoint();m_world.setParticleSystem(entity->id,particles);componentsChanged();return true;
}
void GameEditor::addComponent(Component component) {
    const auto entity=selectedEntity();if(!entity)return;
    if(component==Component::MeshRenderer){if(!entity->renderable)openModelPicker();return;}
    if(component==Component::ReflectionProbe){if(!entity->probe)setProbe(gameplay::ReflectionProbe{});return;}
    if(component==Component::FluidParticles || component==Component::ClothParticles || component==Component::GranularParticles || component==Component::ExplosionParticles || component==Component::EmitterParticles) {
        if(entity->particles)return;
        gameplay::ParticleSystem particles;
        particles.mode=component==Component::ClothParticles?gameplay::ParticleSystem::Mode::Cloth:
            component==Component::GranularParticles?gameplay::ParticleSystem::Mode::Granular:
            component==Component::ExplosionParticles?gameplay::ParticleSystem::Mode::Explosion:
            component==Component::EmitterParticles?gameplay::ParticleSystem::Mode::Emitter:gameplay::ParticleSystem::Mode::Fluid;
        if(particles.mode==gameplay::ParticleSystem::Mode::Cloth)particles.dimensions={24,1,24};
        if(particles.mode==gameplay::ParticleSystem::Mode::Explosion)particles.dimensions={8,8,8};
        setParticles(particles);return;
    }
    if(entity->light)return;gameplay::Light light;
    light.type=component==Component::SpotLight?gameplay::Light::Type::Spot:component==Component::AreaLight?gameplay::Light::Type::Area:gameplay::Light::Type::Point;
    if(light.type==gameplay::Light::Type::Area)light.bias=.05f;setLight(light);
}
void GameEditor::openComponentPicker() {
    m_document.cancelInput();const auto entity=selectedEntity();if(!entity)return;
    std::vector<ResourceItem> items{{"mesh","Mesh Renderer","glTF model and material properties",Icon::Cube,!entity->renderable},
        {"point","Point Light","Omnidirectional local light",Icon::Scene,!entity->light && lightSlotAvailable(gameplay::Light::Type::Point)},
        {"spot","Spot Light","Directional cone with adjustable angles",Icon::Scene,!entity->light && lightSlotAvailable(gameplay::Light::Type::Spot)},
        {"area","Area Light","Rectangular emitter",Icon::Scene,!entity->light && lightSlotAvailable(gameplay::Light::Type::Area)},
        {"probe","Reflection Probe","Box projection and automatic scene capture",Icon::Cube,!entity->probe},
        {"fluid","PhysX Fluid","GPU PBD liquid particles",Icon::Cube,!entity->particles},
        {"cloth","PhysX Cloth","GPU PBD spring cloth",Icon::Cube,!entity->particles},
        {"granular","PhysX Granular","GPU PBD granular particles",Icon::Cube,!entity->particles},
        {"explosion","PhysX Explosion","Burst with blast, fire, smoke and sparks",Icon::Scene,!entity->particles},
        {"emitter","Visual Emitter","Layered particles with editable motion and appearance",Icon::Scene,!entity->particles}};
    m_resourcePicker->open(*m_addComponent,"Add Component",std::move(items),[this,id=entity->id](const std::string& chosen){
        if(m_selected!=id)return;
        addComponent(chosen=="mesh"?Component::MeshRenderer:chosen=="point"?Component::PointLight:chosen=="spot"?Component::SpotLight:chosen=="area"?Component::AreaLight:
            chosen=="fluid"?Component::FluidParticles:chosen=="cloth"?Component::ClothParticles:chosen=="granular"?Component::GranularParticles:chosen=="explosion"?Component::ExplosionParticles:chosen=="emitter"?Component::EmitterParticles:Component::ReflectionProbe);
    });
}
void GameEditor::openModelPicker() {
    m_document.cancelInput();const auto entity=selectedEntity();if(!entity)return;
    std::vector<ResourceItem> items;for(const auto& asset:m_assets)if(asset.model)items.push_back({asset.path.generic_string(),asset.name,asset.path.filename().string(),Icon::Cube});
    m_resourcePicker->open(entity->renderable?*m_resource:*m_addComponent,"Select Mesh",std::move(items),[this,id=entity->id](const std::string& path){if(m_selected==id)assignModel(fs::u8path(path));},true);
}
void GameEditor::buildComponentInspector() {
    auto& content=m_inspector->content();m_meshSection=&mount(content);auto& mesh=section(*m_meshSection,"Mesh Filter");
    auto& meshRow=row(mesh,"Mesh");auto reference=theme::button();reference.grow=1;reference.shrink=1;reference.fontSize=8;reference.textAlign=0;reference.background=theme::input;
    m_resource=&meshRow.button("",[this]{openModelPicker();}).setStyle(reference).setIcon(Icon::Cube,12);
    m_rendererSection=&mount(content);auto& renderer=section(*m_rendererSection,"Mesh Renderer");
    m_materialOverride=&renderer.checkBox("Override material",false,[this](bool value){setMaterial(value?std::optional<gameplay::Renderable::Material>(gameplay::Renderable::Material{}):std::nullopt);});
    auto check=m_materialOverride->style();check.fontSize=9;check.height=20;check.minHeight=0;m_materialOverride->setStyle(check);
    m_particleKinematic=&renderer.checkBox("Moving particle collider",false,[this](bool value){setParticleKinematic(value);}).setStyle(check);
    renderer.label("Follows the object's position and rotation during Play; scale is fixed at Play start.").setStyle(theme::label(8,theme::muted));
    auto info=theme::label(8,theme::muted);info.textWrap=TextWrap::Word;
    renderer.label("Override applies to all model materials; textures are retained.").setStyle(info);
    m_materialBody=&mount(renderer);
    m_materialBody->label("Base color (linear RGB)").setStyle(theme::label(9));
    m_materialColor=std::make_unique<ColorField>(*m_materialBody,[this](auto color){auto e=selectedEntity();if(e && e->renderable && e->renderable->material){auto m=*e->renderable->material;m.color=color;setMaterial(m);}});
    m_materialFields[0]=&number(*m_materialBody,"Metallic",0,1,.05f,[this](float v){auto e=selectedEntity();if(e && e->renderable && e->renderable->material){auto m=*e->renderable->material;m.metallic=v;setMaterial(m);}});
    m_materialFields[1]=&number(*m_materialBody,"Roughness",.04f,1,.05f,[this](float v){auto e=selectedEntity();if(e && e->renderable && e->renderable->material){auto m=*e->renderable->material;m.roughness=v;setMaterial(m);}});
    button(renderer,"Remove Mesh Renderer",[this]{removeMesh();});

    m_lightSection=&mount(content);auto& light=section(*m_lightSection,"Light");
    m_lightEnabled=&light.checkBox("Enabled",true,[this](bool v){auto e=selectedEntity();if(e && e->light){auto l=*e->light;l.enabled=v;setLight(l);}}).setStyle(check);
    auto& types=light.row();const char* names[]{"Point","Spot","Area"};
    for(int i=0;i<3;++i)m_lightTypes[i]=&button(types,names[i],[this,i]{auto e=selectedEntity();if(e && e->light){auto l=*e->light;l.type=gameplay::Light::Type(i);setLight(l);}});
    light.label("Color (linear RGB)").setStyle(theme::label(9));
    m_lightColor=std::make_unique<ColorField>(light,[this](auto color){auto e=selectedEntity();if(e && e->light){auto l=*e->light;l.color=color;setLight(l);}});
    auto editLight=[this](int i,float value){auto e=selectedEntity();if(!e || !e->light)return;auto l=*e->light;
        float* values[]{&l.intensity,&l.range,&l.bias,&l.innerAngle,&l.outerAngle,&l.width,&l.height};*values[i]=value;
        if(i==3)l.outerAngle=std::max(l.outerAngle,l.innerAngle);if(i==4)l.innerAngle=std::min(l.innerAngle,l.outerAngle);setLight(l);};
    m_lightFields[0]=&number(light,"Intensity",0,100000,1,[editLight](float v){editLight(0,v);});
    m_lightFields[1]=&number(light,"Range",.1f,10000,.5f,[editLight](float v){editLight(1,v);});
    m_lightShadows=&light.checkBox("Cast shadows",true,[this](bool v){auto e=selectedEntity();if(e && e->light){auto l=*e->light;l.shadows=v;setLight(l);}}).setStyle(check);
    m_lightFields[2]=&number(light,"Shadow bias",0,1,.005f,[editLight](float v){editLight(2,v);});
    m_spotFields=&mount(light);m_lightFields[3]=&number(*m_spotFields,"Inner angle",0,89,1,[editLight](float v){editLight(3,v);});
    m_lightFields[4]=&number(*m_spotFields,"Outer angle",.1f,89,1,[editLight](float v){editLight(4,v);});
    m_areaFields=&mount(light);m_lightFields[5]=&number(*m_areaFields,"Width",.01f,1000,.1f,[editLight](float v){editLight(5,v);});
    m_lightFields[6]=&number(*m_areaFields,"Height",.01f,1000,.1f,[editLight](float v){editLight(6,v);});
    light.label("Spot/area direction follows the object's local -Y axis. Scene limit: 4 point, 4 spot, 2 area lights, including setup scripts.").setStyle(info);
    button(light,"Remove Light",[this]{setLight(std::nullopt);});

    m_probeSection=&mount(content);auto& probe=section(*m_probeSection,"Reflection Probe");
    m_probeEnabled=&probe.checkBox("Enabled",true,[this](bool v){auto e=selectedEntity();if(e && e->probe){auto p=*e->probe;p.enabled=v;setProbe(p);}}).setStyle(check);
    auto editProbe=[this](int i,float value){auto e=selectedEntity();if(!e || !e->probe)return;auto p=*e->probe;
        if(i<3)p.size[i]=value;else if(i==3)p.blend=value;else p.priority=int(std::round(value));setProbe(p);};
    const char* sizeNames[]{"Size X","Size Y","Size Z"};
    for(int i=0;i<3;++i)m_probeFields[i]=&number(probe,sizeNames[i],.1f,10000,.5f,[editProbe,i](float v){editProbe(i,v);});
    m_probeFields[3]=&number(probe,"Blend distance",0,10000,.1f,[editProbe](float v){editProbe(3,v);});
    m_probeFields[4]=&number(probe,"Priority",-1000,1000,1,[editProbe](float v){editProbe(4,v);});
    probe.label("Axis-aligned box at object position; size follows object scale. Reflections recapture automatically. Two probes blend at a time.").setStyle(info);
    button(probe,"Remove Reflection Probe",[this]{setProbe(std::nullopt);});
    m_particleSection=&mount(content);auto& particles=section(*m_particleSection,"Particle System");
    m_particleEnabled=&particles.checkBox("Enabled",true,[this](bool v){auto e=selectedEntity();if(e && e->particles){auto p=*e->particles;p.enabled=v;setParticles(p);}}).setStyle(check);
    auto& particleTypes=particles.row();const char* particleNames[]{"Emitter","Fluid","Cloth","Granular","Explosion"};
    for(int i=0;i<5;++i)m_particleTypes[i]=&button(particleTypes,particleNames[i],[this,i]{
        constexpr gameplay::ParticleSystem::Mode modes[]{gameplay::ParticleSystem::Mode::Emitter,
            gameplay::ParticleSystem::Mode::Fluid,gameplay::ParticleSystem::Mode::Cloth,
            gameplay::ParticleSystem::Mode::Granular,gameplay::ParticleSystem::Mode::Explosion};
        auto e=selectedEntity();if(!e || !e->particles)return;auto p=*e->particles;p.mode=modes[i];
        if(p.mode==gameplay::ParticleSystem::Mode::Cloth)p.dimensions[1]=1;setParticles(p);
    });
    m_particlePhysicalFields=&mount(particles);auto& physical=*m_particlePhysicalFields;
    auto editParticle=[this](int i,float value){auto e=selectedEntity();if(!e || !e->particles)return;auto p=*e->particles;
        if(i<3)p.dimensions[i]=int(value);
        else {float* values[]{&p.spacing,&p.mass,&p.friction,&p.damping,&p.viscosity,&p.cohesion,&p.stiffness};*values[i-3]=value;}
        setParticles(p);
    };
    m_particleFields[0]=&number(physical,"Count X",1,128,1,[editParticle](float v){editParticle(0,v);});
    m_particleFields[1]=&number(physical,"Count Y",1,128,1,[editParticle](float v){editParticle(1,v);});
    m_particleFields[2]=&number(physical,"Count Z",1,128,1,[editParticle](float v){editParticle(2,v);});
    m_particleFields[3]=&number(physical,"Spacing",.01f,2,.01f,[editParticle](float v){editParticle(3,v);});
    m_particleFields[4]=&number(physical,"Mass",.0001f,100,.01f,[editParticle](float v){editParticle(4,v);});
    m_particleFields[5]=&number(physical,"Friction",0,1,.05f,[editParticle](float v){editParticle(5,v);});
    m_particleFields[6]=&number(physical,"Damping",0,1,.01f,[editParticle](float v){editParticle(6,v);});
    m_particleFluidFields=&mount(physical);
    m_particleFields[7]=&number(*m_particleFluidFields,"Viscosity",0,100,.01f,[editParticle](float v){editParticle(7,v);});
    m_particleFields[8]=&number(*m_particleFluidFields,"Cohesion",0,100,.01f,[editParticle](float v){editParticle(8,v);});
    m_particleClothFields=&mount(physical);
    m_particleFields[9]=&number(*m_particleClothFields,"Stiffness",0,1000000,100,[editParticle](float v){editParticle(9,v);});
    m_particleClothFields->label("Pin cloth X edge to this object's transform").setStyle(info);
    auto& pinEdges=m_particleClothFields->row();const char* pinNames[]{"None","Left","Right","Both"};
    for(int i=0;i<4;++i)m_particlePinEdges[i]=&button(pinEdges,pinNames[i],[this,i]{
        auto e=selectedEntity();if(!e || !e->particles)return;auto p=*e->particles;p.pinEdge=gameplay::ParticleSystem::PinEdge(i);setParticles(p);
    });
    m_particleExplosionFields=&mount(physical);
    auto editExplosion=[this](int i,float value){auto e=selectedEntity();if(!e || !e->particles)return;auto p=*e->particles;
        float* values[]{&p.burstSpeed,&p.blastRadius,&p.blastImpulse,&p.effectDuration};*values[i]=value;setParticles(p);};
    m_particleExplosionValues[0]=&number(*m_particleExplosionFields,"Burst speed",.1f,100,.5f,[editExplosion](float v){editExplosion(0,v);});
    m_particleExplosionValues[1]=&number(*m_particleExplosionFields,"Blast radius",.1f,100,.1f,[editExplosion](float v){editExplosion(1,v);});
    m_particleExplosionValues[2]=&number(*m_particleExplosionFields,"Cloth impulse",0,100,.5f,[editExplosion](float v){editExplosion(2,v);});
    m_particleExplosionValues[3]=&number(*m_particleExplosionFields,"Effect duration",.1f,30,.1f,[editExplosion](float v){editExplosion(3,v);});
    physical.label("PhysX modes require a CUDA-capable NVIDIA GPU. Maximum 65,536 particles.").setStyle(info);
    m_emitterSection=&mount(particles);auto& emitter=*m_emitterSection;
    emitter.label("Emitter layers").setStyle(theme::label(9));
    auto& layerNav=emitter.row();
    m_emitterPrev=&button(layerNav,"<",[this]{if(m_selectedEmitterLayer){--m_selectedEmitterLayer;updateInspector();}});
    m_emitterLayerLabel=&layerNav.label("").setStyle(theme::label(9));
    m_emitterNext=&button(layerNav,">",[this]{auto e=selectedEntity();if(e && e->particles && m_selectedEmitterLayer+1<e->particles->layers.size()){
        ++m_selectedEmitterLayer;updateInspector();}});
    auto& layerActions=emitter.row();
    m_emitterAdd=&button(layerActions,"Add layer",[this]{auto e=selectedEntity();if(!e || !e->particles)return;
        auto p=*e->particles;if(p.layers.size()>=8)return;
        int count=0;for(const auto& layer:p.layers)count+=layer.count;
        if(count+64>4096)return;
        gameplay::ParticleSystem::Layer layer;layer.name="Layer "+std::to_string(p.layers.size()+1);
        p.layers.push_back(layer);m_selectedEmitterLayer=p.layers.size()-1;setParticles(p);
    });
    m_emitterRemove=&button(layerActions,"Remove layer",[this]{auto e=selectedEntity();if(!e || !e->particles)return;
        auto p=*e->particles;if(p.layers.size()<=1 || m_selectedEmitterLayer>=p.layers.size())return;
        p.layers.erase(p.layers.begin()+m_selectedEmitterLayer);
        m_selectedEmitterLayer=std::min(m_selectedEmitterLayer,p.layers.size()-1);setParticles(p);
    });
    m_emitterName=&emitter.textField("",{}).setStyle(theme::field());
    m_emitterName->setPlaceholder("Layer name").onTextCommitted([this](const std::string& name){
        auto e=selectedEntity();if(!e || !e->particles || m_selectedEmitterLayer>=e->particles->layers.size())return;
        auto p=*e->particles;p.layers[m_selectedEmitterLayer].name=name;setParticles(p);
    });
    auto& shapes=emitter.row();
    m_emitterShapes[0]=&button(shapes,"Soft",[this]{auto e=selectedEntity();if(!e || !e->particles)return;
        auto p=*e->particles;p.layers[m_selectedEmitterLayer].shape=gameplay::ParticleSystem::Layer::Shape::Soft;setParticles(p);});
    m_emitterShapes[1]=&button(shapes,"Spark",[this]{auto e=selectedEntity();if(!e || !e->particles)return;
        auto p=*e->particles;p.layers[m_selectedEmitterLayer].shape=gameplay::ParticleSystem::Layer::Shape::Spark;setParticles(p);});
    auto editEmitter=[this](int i,float value){auto e=selectedEntity();if(!e || !e->particles || m_selectedEmitterLayer>=e->particles->layers.size())return;
        auto p=*e->particles;auto& l=p.layers[m_selectedEmitterLayer];
        if(i==0)l.count=int(value);
        else if(i==1)l.lifetime=value;
        else if(i==2)l.spawnRadius=value;
        else if(i>=3 && i<=5)l.offset[i-3]=value;
        else if(i>=6 && i<=8)l.velocity[i-6]=value;
        else if(i>=9 && i<=11)l.acceleration[i-9]=value;
        else if(i==12)l.radialSpeed=value;
        else if(i==13)l.turbulence=value;
        else if(i==14)l.size=value;
        else if(i==15)l.sizeGrowth=value;
        else if(i==16)l.startColor[3]=value;
        else if(i==17)l.endColor[3]=value;
        else l.fadeIn=value;
        setParticles(p);
    };
    emitter.label("Emission").setStyle(theme::label(9));
    m_emitterFields[0]=&number(emitter,"Count",1,4096,1,[editEmitter](float v){editEmitter(0,v);});
    m_emitterFields[1]=&number(emitter,"Lifetime",.05f,30,.05f,[editEmitter](float v){editEmitter(1,v);});
    m_emitterFields[2]=&number(emitter,"Spawn radius",0,10,.05f,[editEmitter](float v){editEmitter(2,v);});
    const char* offsets[]{"Offset X","Offset Y","Offset Z"};
    for(int i=0;i<3;++i)m_emitterFields[3+i]=&number(emitter,offsets[i],-100,100,.1f,[editEmitter,i](float v){editEmitter(3+i,v);});
    emitter.label("Motion").setStyle(theme::label(9));
    const char* velocities[]{"Velocity X","Velocity Y","Velocity Z"};
    for(int i=0;i<3;++i)m_emitterFields[6+i]=&number(emitter,velocities[i],-100,100,.1f,[editEmitter,i](float v){editEmitter(6+i,v);});
    const char* accelerations[]{"Accel X","Accel Y","Accel Z"};
    for(int i=0;i<3;++i)m_emitterFields[9+i]=&number(emitter,accelerations[i],-100,100,.1f,[editEmitter,i](float v){editEmitter(9+i,v);});
    m_emitterFields[12]=&number(emitter,"Radial speed",-100,100,.05f,[editEmitter](float v){editEmitter(12,v);});
    m_emitterFields[13]=&number(emitter,"Turbulence",0,100,.05f,[editEmitter](float v){editEmitter(13,v);});
    emitter.label("Appearance").setStyle(theme::label(9));
    m_emitterFields[14]=&number(emitter,"Size",.005f,10,.01f,[editEmitter](float v){editEmitter(14,v);});
    m_emitterFields[15]=&number(emitter,"Size growth",-10,10,.01f,[editEmitter](float v){editEmitter(15,v);});
    emitter.label("Start color").setStyle(theme::label(9));
    m_emitterStartColor=std::make_unique<ColorField>(emitter,[this](auto color){auto e=selectedEntity();if(!e || !e->particles)return;
        auto p=*e->particles;auto& c=p.layers[m_selectedEmitterLayer].startColor;for(int i=0;i<3;++i)c[i]=color[i];setParticles(p);});
    m_emitterFields[16]=&number(emitter,"Start alpha",0,1,.05f,[editEmitter](float v){editEmitter(16,v);});
    emitter.label("End color").setStyle(theme::label(9));
    m_emitterEndColor=std::make_unique<ColorField>(emitter,[this](auto color){auto e=selectedEntity();if(!e || !e->particles)return;
        auto p=*e->particles;auto& c=p.layers[m_selectedEmitterLayer].endColor;for(int i=0;i<3;++i)c[i]=color[i];setParticles(p);});
    m_emitterFields[17]=&number(emitter,"End alpha",0,1,.05f,[editEmitter](float v){editEmitter(17,v);});
    m_emitterFields[18]=&number(emitter,"Fade in",0,1,.05f,[editEmitter](float v){editEmitter(18,v);});
    emitter.label("Visual layers loop during Play. Up to 8 layers and 4,096 particles total.").setStyle(info);
    button(particles,"Remove Particle System",[this]{setParticles(std::nullopt);});
    auto& add=mount(content);auto s=add.style();s.padding=8;add.setStyle(s);m_addComponent=&button(add,"Add Component",[this]{openComponentPicker();});
}
void GameEditor::updateComponentInspector(const gameplay::EntitySnapshot& entity) {
    m_meshSection->setVisible(entity.renderable.has_value());m_rendererSection->setVisible(entity.renderable.has_value());
    if(entity.renderable)m_particleKinematic->setChecked(entity.renderable->particleKinematic);
    const bool material=entity.renderable && entity.renderable->material;m_materialOverride->setChecked(material);m_materialBody->setVisible(material);
    if(material){const auto& m=*entity.renderable->material;m_materialColor->setValue(m.color);m_materialFields[0]->setValue(m.metallic);m_materialFields[1]->setValue(m.roughness);}
    m_lightSection->setVisible(entity.light.has_value());
    if(entity.light) {
        const auto& l=*entity.light;m_lightEnabled->setChecked(l.enabled);m_lightShadows->setChecked(l.shadows);m_lightColor->setValue(l.color);
        const float values[]{l.intensity,l.range,l.bias,l.innerAngle,l.outerAngle,l.width,l.height};for(int i=0;i<7;++i)m_lightFields[i]->setValue(values[i]);
        m_spotFields->setVisible(l.type==gameplay::Light::Type::Spot);m_areaFields->setVisible(l.type==gameplay::Light::Type::Area);
        for(int i=0;i<3;++i){auto s=m_lightTypes[i]->style();s.background=i==int(l.type)?theme::selection:theme::raised;m_lightTypes[i]->setStyle(s);}
    }
    m_probeSection->setVisible(entity.probe.has_value());
    if(entity.probe){const auto& p=*entity.probe;m_probeEnabled->setChecked(p.enabled);for(int i=0;i<3;++i)m_probeFields[i]->setValue(p.size[i]);m_probeFields[3]->setValue(p.blend);m_probeFields[4]->setValue(float(p.priority));}
    m_particleSection->setVisible(entity.particles.has_value());
    if(entity.particles){const auto& p=*entity.particles;m_particleEnabled->setChecked(p.enabled);
        for(int i=0;i<3;++i)m_particleFields[i]->setValue(float(p.dimensions[i]));
        constexpr gameplay::ParticleSystem::Mode modes[]{gameplay::ParticleSystem::Mode::Emitter,
            gameplay::ParticleSystem::Mode::Fluid,gameplay::ParticleSystem::Mode::Cloth,
            gameplay::ParticleSystem::Mode::Granular,gameplay::ParticleSystem::Mode::Explosion};
        for(int i=0;i<5;++i){auto s=m_particleTypes[i]->style();s.background=modes[i]==p.mode?theme::selection:theme::raised;m_particleTypes[i]->setStyle(s);}
        const float values[]{p.spacing,p.mass,p.friction,p.damping,p.viscosity,p.cohesion,p.stiffness};
        for(int i=0;i<7;++i)m_particleFields[i+3]->setValue(values[i]);
        m_particlePhysicalFields->setVisible(p.mode!=gameplay::ParticleSystem::Mode::Emitter);
        m_emitterSection->setVisible(p.mode==gameplay::ParticleSystem::Mode::Emitter);
        m_particleFields[1]->setEnabled(p.mode!=gameplay::ParticleSystem::Mode::Cloth);
        m_particleFluidFields->setVisible(p.mode==gameplay::ParticleSystem::Mode::Fluid);
        m_particleClothFields->setVisible(p.mode==gameplay::ParticleSystem::Mode::Cloth);
        m_particleExplosionFields->setVisible(p.mode==gameplay::ParticleSystem::Mode::Explosion);
        const float explosionValues[]{p.burstSpeed,p.blastRadius,p.blastImpulse,p.effectDuration};
        for(int i=0;i<4;++i)m_particleExplosionValues[i]->setValue(explosionValues[i]);
        for(int i=0;i<4;++i){auto s=m_particlePinEdges[i]->style();s.background=i==int(p.pinEdge)?theme::selection:theme::raised;m_particlePinEdges[i]->setStyle(s);}
        if(p.mode==gameplay::ParticleSystem::Mode::Emitter && !p.layers.empty()){
            m_selectedEmitterLayer=std::min(m_selectedEmitterLayer,p.layers.size()-1);
            const auto& layer=p.layers[m_selectedEmitterLayer];
            m_emitterLayerLabel->setText("Layer "+std::to_string(m_selectedEmitterLayer+1)+" / "+std::to_string(p.layers.size()));
            m_emitterPrev->setEnabled(m_selectedEmitterLayer>0);m_emitterNext->setEnabled(m_selectedEmitterLayer+1<p.layers.size());
            m_emitterAdd->setEnabled(p.layers.size()<8);m_emitterRemove->setEnabled(p.layers.size()>1);
            m_emitterName->setText(layer.name);
            for(int i=0;i<2;++i){auto s=m_emitterShapes[i]->style();s.background=i==int(layer.shape)?theme::selection:theme::raised;m_emitterShapes[i]->setStyle(s);}
            const float layerValues[]{float(layer.count),layer.lifetime,layer.spawnRadius,
                layer.offset[0],layer.offset[1],layer.offset[2],
                layer.velocity[0],layer.velocity[1],layer.velocity[2],
                layer.acceleration[0],layer.acceleration[1],layer.acceleration[2],
                layer.radialSpeed,layer.turbulence,layer.size,layer.sizeGrowth,
                layer.startColor[3],layer.endColor[3],layer.fadeIn};
            for(int i=0;i<19;++i)m_emitterFields[i]->setValue(layerValues[i]);
            m_emitterStartColor->setValue({layer.startColor[0],layer.startColor[1],layer.startColor[2]});
            m_emitterEndColor->setValue({layer.endColor[0],layer.endColor[1],layer.endColor[2]});
        }
    }
}
}
