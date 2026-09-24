#pragma once

#include <array>
#include <cmath>
#include <cstdint>
#include <span>
#include <vector>

namespace genesis::materials {

using TangentVec = std::array<float, 3>;
inline float tangentDot(const TangentVec& a, const TangentVec& b) {
    return a[0]*b[0] + a[1]*b[1] + a[2]*b[2];
}
inline TangentVec tangentCross(const TangentVec& a, const TangentVec& b) {
    return {a[1]*b[2]-a[2]*b[1], a[2]*b[0]-a[0]*b[2], a[0]*b[1]-a[1]*b[0]};
}
inline TangentVec tangentNormalize(TangentVec value, TangentVec fallback) {
    const float squared = tangentDot(value, value);
    if (!std::isfinite(squared) || squared <= 1e-20f) return fallback;
    for (float& component : value) component /= std::sqrt(squared);
    return value;
}

// The caller supplies the normal texture's final UVs, not the base-color UVs.
// Authored tangents bypass generation. This smooth triangle-derivative fallback
// is not MikkTSpace; exporters should supply tangents for exact baked seam fidelity.
template<class Vertex, class Uv>
void generateTangents(std::span<Vertex> vertices, std::span<const uint32_t> indices, Uv uv) {
    std::vector<TangentVec> tangentSum(vertices.size()), bitangentSum(vertices.size());
    for (size_t i = 0; i + 2 < indices.size(); i += 3) {
        const uint32_t a = indices[i], b = indices[i+1], c = indices[i+2];
        if (a >= vertices.size() || b >= vertices.size() || c >= vertices.size()) continue;
        const auto ua = uv(vertices[a]), ub = uv(vertices[b]), uc = uv(vertices[c]);
        const double du1 = double(ub[0])-ua[0], dv1 = double(ub[1])-ua[1];
        const double du2 = double(uc[0])-ua[0], dv2 = double(uc[1])-ua[1];
        const double det = du1*dv2-dv1*du2;
        // Relative degeneracy check preserves small, valid UV atlas regions.
        if (!std::isfinite(det) || std::abs(det) <= 1e-12 * (std::abs(du1*dv2)+std::abs(dv1*du2))) continue;
        for (size_t axis = 0; axis < 3; ++axis) {
            const double e1 = double(vertices[b].position[axis])-vertices[a].position[axis];
            const double e2 = double(vertices[c].position[axis])-vertices[a].position[axis];
            const float t = float((e1*dv2-e2*dv1)/det), bt = float((e2*du1-e1*du2)/det);
            if (!std::isfinite(t) || !std::isfinite(bt)) continue;
            for (uint32_t index : {a, b, c}) { tangentSum[index][axis] += t; bitangentSum[index][axis] += bt; }
        }
    }
    for (size_t i = 0; i < vertices.size(); ++i) {
        auto& vertex = vertices[i];
        const TangentVec n = tangentNormalize({vertex.normal[0],vertex.normal[1],vertex.normal[2]}, {0,1,0});
        TangentVec t = tangentSum[i];
        const float projection = tangentDot(n, t);
        for (size_t axis = 0; axis < 3; ++axis) t[axis] -= n[axis]*projection;
        const TangentVec helper = std::abs(n[2]) < .9f ? TangentVec{0,0,1} : TangentVec{0,1,0};
        t = tangentNormalize(t, tangentNormalize(tangentCross(helper, n), {1,0,0}));
        for (size_t axis = 0; axis < 3; ++axis) vertex.tangent[axis] = t[axis];
        vertex.tangent[3] = tangentDot(tangentCross(n,t), bitangentSum[i]) < 0 ? -1.0f : 1.0f;
    }
}

} // namespace genesis::materials
