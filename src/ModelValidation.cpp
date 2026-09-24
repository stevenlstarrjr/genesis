#include "ModelValidation.h"
#define CGLTF_IMPLEMENTATION
#include <cgltf.h>
namespace genesis {
bool validateModel(const std::filesystem::path& path,std::string& error) {
    cgltf_options options{};cgltf_data* data=nullptr;
    if(cgltf_parse_file(&options,path.string().c_str(),&data)!=cgltf_result_success || !data){error="Cannot parse model: "+path.filename().string();return false;}
    const bool valid=cgltf_load_buffers(&options,data,path.string().c_str())==cgltf_result_success && cgltf_validate(data)==cgltf_result_success && data->meshes_count>0;
    cgltf_free(data);if(!valid)error="Model has invalid or missing mesh data: "+path.filename().string();return valid;
}
}
