$input v_worldPos, v_normal, v_tangent, v_materialUv0, v_materialUv1, v_occlusionUv, v_shadowPos0, v_shadowPos1, v_shadowPos2

#include <bgfx_shader.sh>
#include "hdr_output.sh"
#include "environment_diffuse.sh"

SAMPLER2D(s_baseColor, 0);
SAMPLER2D(s_metalRough, 1);
SAMPLER2D(s_normal, 2);
SAMPLER2D(s_emissive, 3);
SAMPLER2DSHADOW(s_shadow0, 4);
SAMPLER2DSHADOW(s_shadow1, 5);
SAMPLER2DSHADOW(s_shadow2, 6);
SAMPLER2D(s_environmentIrradiance, 7);
SAMPLERCUBE(s_environmentSpecular, 8);
SAMPLER2D(s_brdfLut, 9);
SAMPLER2DARRAY(s_localReflectionProbe, 10);
SAMPLER2DARRAY(s_localDiffuseProbe, 11);
SAMPLER2D(s_gridAtlas, 12);
SAMPLER2D(s_occlusion, 13);
SAMPLERCUBESHADOW(s_pointShadow0, 14);
SAMPLER2DARRAYSHADOW(s_pointShadow1, 15);

uniform vec4 u_cameraPos;
uniform vec4 u_lightDirIntensity;
uniform vec4 u_lightColor;
uniform vec4 u_ambientSky;
uniform vec4 u_ambientGround;
uniform vec4 u_baseColorFactor;
uniform vec4 u_materialParams; // metallic, roughness, normal scale, alpha cutoff
uniform vec4 u_emissiveFactor;
uniform vec4 u_textureFlags;   // base, metal/rough, normal, emissive
uniform vec4 u_materialOcclusion; // strength; zero disables sampling
uniform vec4 u_selectionId;
uniform vec4 u_shadowParams;   // map size, bias, enabled, unused
uniform vec4 u_cascadeSplits;  // near, middle, far, transition fraction
uniform vec4 u_atmosphereParams; // bottom radius, top radius, altitude km, sky scale
uniform vec4 u_environmentParams; // atmosphere diffuse strength, specular strength, max probe mip, HDRI enabled
uniform vec4 u_reflectionProbePosition[2]; // capture position, enabled
uniform vec4 u_reflectionProbeMin[2]; // influence minimum, blend distance
uniform vec4 u_reflectionProbeMax[2]; // influence maximum, priority
uniform vec4 u_skyForward; // camera forward direction for cascade selection
uniform vec4 u_gridOrigin;  // first probe position, enabled
uniform vec4 u_gridSpacing; // xyz spacing
uniform vec4 u_gridCounts;  // xyz counts, total count
uniform vec4 u_pointLightPositionRadius0; // xyz position, radius; radius zero disables
uniform vec4 u_pointLightPositionRadius1;
uniform vec4 u_pointLightPositionRadius2;
uniform vec4 u_pointLightPositionRadius3;
uniform vec4 u_pointLightColorIntensity0; // linear RGB, intensity
uniform vec4 u_pointLightColorIntensity1;
uniform vec4 u_pointLightColorIntensity2;
uniform vec4 u_pointLightColorIntensity3;
uniform vec4 u_pointLightShadow[4]; // shadow slot + 1, world bias, near, far
uniform vec4 u_pointLightPattern[4]; // x: IES atlas layer + 1
uniform vec4 u_spotLightPositionRadius[4];
uniform vec4 u_spotLightDirectionOuter[4]; // xyz direction from light, cosine outer angle
uniform vec4 u_spotLightColorIntensity[4];
uniform vec4 u_spotLightParams[4]; // cosine inner angle, unused, IES atlas layer + 1
uniform vec4 u_spotLightShadow[4];
uniform vec4 u_spotLightUpPattern[4]; // xyz oriented up, cookie atlas layer + 1
uniform vec4 u_areaLightPositionRadius[2];
uniform vec4 u_areaLightDirectionWidth[2]; // xyz emission direction, width
uniform vec4 u_areaLightUpHeight[2]; // xyz rectangle up, height
uniform vec4 u_areaLightColorIntensity[2];
uniform vec4 u_areaLightShadow[2];
uniform vec4 u_emissiveLightPositionRadius[4]; // xyz centroid, influence radius
uniform vec4 u_emissiveLightDirectionArea[4]; // xyz normal, signed area (negative is two-sided)
uniform vec4 u_emissiveLightRadiance[4]; // rgb linear radiance, w panel half-height
uniform vec4 u_emissiveLightShadow[4]; // selected local shadow slot, bias, near, far
uniform vec4 u_emissiveLightShadowSecondary[4]; // optional second capture slot for a wide emitter
uniform vec4 u_emissiveLightShadowOrigin[4]; // actual capture origin when nearby clusters share a slot
uniform vec4 u_emissiveLightTangentWidth[4]; // xyz panel tangent, w half-width
uniform vec4 u_pointShadowAtlasParams; // inverse face size, PCF radius in texels

vec2 gridVisibilityUv(vec3 direction, float probeIndex)
{
    vec3 absoluteDirection = abs(direction);
    float face = 0.0;
    vec2 faceUv;
    if (absoluteDirection.x >= absoluteDirection.y && absoluteDirection.x >= absoluteDirection.z)
    {
        if (direction.x >= 0.0) { face = 0.0; faceUv = vec2(-direction.z, direction.y) / absoluteDirection.x; }
        else { face = 1.0; faceUv = vec2(direction.z, direction.y) / absoluteDirection.x; }
    }
    else if (absoluteDirection.y >= absoluteDirection.z)
    {
        if (direction.y >= 0.0) { face = 2.0; faceUv = vec2(direction.x, -direction.z) / absoluteDirection.y; }
        else { face = 3.0; faceUv = vec2(direction.x, direction.z) / absoluteDirection.y; }
    }
    else
    {
        if (direction.z >= 0.0) { face = 4.0; faceUv = vec2(direction.x, direction.y) / absoluteDirection.z; }
        else { face = 5.0; faceUv = vec2(-direction.x, direction.y) / absoluteDirection.z; }
    }
    vec2 uv = vec2(faceUv.x * 0.5 + 0.5, 0.5 - faceUv.y * 0.5);
    float resolution = 16.0;
    return vec2((probeIndex * resolution + uv.x * (resolution - 1.0) + 0.5)
            / (u_gridCounts.w * resolution),
        (face * resolution + uv.y * (resolution - 1.0) + 0.5) / (6.0 * resolution));
}

vec4 gridProbeLighting(float probeIndex, vec3 normal)
{
    vec3 axisWeight = abs(normal);
    axisWeight /= max(axisWeight.x + axisWeight.y + axisWeight.z, 1e-4);
    float faceX = normal.x >= 0.0 ? 0.0 : 1.0;
    float faceY = normal.y >= 0.0 ? 2.0 : 3.0;
    float faceZ = normal.z >= 0.0 ? 4.0 : 5.0;
    // See lighting/ProbeGridAtlas.h. The last six rows hold the original
    // coefficients; all preceding rows pack four visibility distances in RGBA.
    float probeUv = (probeIndex + 0.5) / (u_gridCounts.w * 4.0);
    vec4 x = texture2DLod(s_gridAtlas, vec2(probeUv, (96.0 + faceX + 0.5) / 102.0), 0.0);
    vec4 y = texture2DLod(s_gridAtlas, vec2(probeUv, (96.0 + faceY + 0.5) / 102.0), 0.0);
    vec4 z = texture2DLod(s_gridAtlas, vec2(probeUv, (96.0 + faceZ + 0.5) / 102.0), 0.0);
    return x * axisWeight.x + y * axisWeight.y + z * axisWeight.z;
}

float gridProbeDistance(vec3 direction, float probeIndex)
{
    vec2 texel = floor(gridVisibilityUv(direction, probeIndex) * vec2(u_gridCounts.w * 16.0, 96.0));
    vec2 uv = vec2((floor(texel.x / 4.0) + 0.5) / (u_gridCounts.w * 4.0),
                   (texel.y + 0.5) / 102.0);
    vec4 distances = texture2DLod(s_gridAtlas, uv, 0.0);
    float channel = mod(texel.x, 4.0);
    return channel < 0.5 ? distances.x : (channel < 1.5 ? distances.y
        : (channel < 2.5 ? distances.z : distances.w));
}

vec4 sampleGridLighting(vec3 worldPosition, vec3 normal)
{
    vec3 gridPosition = (worldPosition - u_gridOrigin.xyz) / u_gridSpacing.xyz;
    vec3 base = clamp(floor(gridPosition), vec3_splat(0.0), u_gridCounts.xyz - 2.0);
    vec3 fraction = saturate(gridPosition - base);
    vec4 accumulated = vec4_splat(0.0);
    float totalWeight = 0.0;
    for (int corner = 0; corner < 8; ++corner)
    {
        vec3 offset = vec3(float(corner & 1), float((corner >> 1) & 1), float((corner >> 2) & 1));
        vec3 coordinate = base + offset;
        float probeIndex = coordinate.x + u_gridCounts.x * (coordinate.y + u_gridCounts.y * coordinate.z);
        vec3 probePosition = u_gridOrigin.xyz + coordinate * u_gridSpacing.xyz;
        vec3 toSurface = worldPosition - probePosition;
        float surfaceDistance = length(toSurface);
        float storedDistance = gridProbeDistance(toSurface / max(surfaceDistance, 1e-4), probeIndex);
        float tolerance = max(0.20, min(u_gridSpacing.x, min(u_gridSpacing.y, u_gridSpacing.z)) * 0.12);
        float visible = saturate((storedDistance - surfaceDistance + tolerance) / tolerance);
        vec3 trilinear = mix(1.0 - fraction, fraction, offset);
        float weight = trilinear.x * trilinear.y * trilinear.z * visible;
        accumulated += gridProbeLighting(probeIndex, normal) * weight;
        totalWeight += weight;
    }
    return accumulated / max(totalWeight, 1e-4);
}

vec3 cubeAtlasCoordinate(vec3 direction, float probeSlot)
{
    vec3 absoluteDirection = abs(direction);
    float face = 0.0;
    vec2 faceUv;
    if (absoluteDirection.x >= absoluteDirection.y && absoluteDirection.x >= absoluteDirection.z)
    {
        if (direction.x >= 0.0) { face = 0.0; faceUv = vec2(-direction.z, direction.y) / absoluteDirection.x; }
        else { face = 1.0; faceUv = vec2(direction.z, direction.y) / absoluteDirection.x; }
    }
    else if (absoluteDirection.y >= absoluteDirection.z)
    {
        if (direction.y >= 0.0) { face = 2.0; faceUv = vec2(direction.x, -direction.z) / absoluteDirection.y; }
        else { face = 3.0; faceUv = vec2(direction.x, direction.z) / absoluteDirection.y; }
    }
    else
    {
        if (direction.z >= 0.0) { face = 4.0; faceUv = vec2(direction.x, direction.y) / absoluteDirection.z; }
        else { face = 5.0; faceUv = vec2(-direction.x, direction.y) / absoluteDirection.z; }
    }
    return vec3(faceUv.x * 0.5 + 0.5, 0.5 - faceUv.y * 0.5, probeSlot * 6.0 + face);
}

float distributionGGX(float NoH, float roughness)
{
    float a = roughness * roughness;
    float a2 = a * a;
    float d = NoH * NoH * (a2 - 1.0) + 1.0;
    return a2 / max(3.14159265 * d * d, 1e-5);
}

float geometrySchlickGGX(float NoV, float roughness)
{
    float r = roughness + 1.0;
    float k = r * r * 0.125;
    return NoV / max(NoV * (1.0 - k) + k, 1e-5);
}

vec3 fresnelSchlick(float cosTheta, vec3 f0)
{
    return f0 + (1.0 - f0) * pow(saturate(1.0 - cosTheta), 5.0);
}

vec3 boxProjectedDirection(vec3 worldPosition, vec3 reflection, vec4 probePosition,
    vec4 probeMinimum, vec4 probeMaximum)
{
    vec3 safeDirection = reflection;
    safeDirection.x = abs(safeDirection.x) < 1e-4 ? (safeDirection.x < 0.0 ? -1e-4 : 1e-4) : safeDirection.x;
    safeDirection.y = abs(safeDirection.y) < 1e-4 ? (safeDirection.y < 0.0 ? -1e-4 : 1e-4) : safeDirection.y;
    safeDirection.z = abs(safeDirection.z) < 1e-4 ? (safeDirection.z < 0.0 ? -1e-4 : 1e-4) : safeDirection.z;
    vec3 toMinimum = (probeMinimum.xyz - worldPosition) / safeDirection;
    vec3 toMaximum = (probeMaximum.xyz - worldPosition) / safeDirection;
    vec3 distances = mix(toMinimum, toMaximum, step(vec3_splat(0.0), safeDirection));
    float distance = min(distances.x, min(distances.y, distances.z));
    return worldPosition + safeDirection * max(distance, 0.0) - probePosition.xyz;
}

float reflectionProbeWeight(vec3 worldPosition, vec4 probePosition,
    vec4 probeMinimum, vec4 probeMaximum)
{
    vec3 edgeDistance = min(worldPosition - probeMinimum.xyz,
        probeMaximum.xyz - worldPosition);
    float insideDistance = min(edgeDistance.x, min(edgeDistance.y, edgeDistance.z));
    return probePosition.w * saturate(insideDistance / max(probeMinimum.w, 1e-4));
}

float shadowTap(vec4 lightPosition, float NoL, int cascade)
{
    if (u_shadowParams.z < 0.5)
        return 1.0;

    vec3 p = lightPosition.xyz / lightPosition.w;
    vec2 uv = p.xy;
    if (min(min(uv.x, uv.y), min(1.0 - uv.x, 1.0 - uv.y)) < 0.0 || p.z < 0.0 || p.z > 1.0)
        return 1.0;

    float texel = 1.0 / max(u_shadowParams.x, 1.0);
    float result = 0.0;
    for (int y = -1; y <= 1; ++y)
    {
        for (int x = -1; x <= 1; ++x)
        {
            vec3 coord = vec3(uv + vec2(float(x), float(y)) * texel,
                p.z - u_shadowParams.y * (1.25 - NoL));
            if (cascade == 0) result += shadow2D(s_shadow0, coord);
            else if (cascade == 1) result += shadow2D(s_shadow1, coord);
            else result += shadow2D(s_shadow2, coord);
        }
    }
    return result / 9.0;
}

float shadowVisibility(float viewDistance, float NoL, vec4 shadowPosition0, vec4 shadowPosition1, vec4 shadowPosition2)
{
    float visibility = 1.0;
    if (viewDistance < u_cascadeSplits.x)
    {
        float nearShadow = shadowTap(shadowPosition0, NoL, 0);
        float blendStart = u_cascadeSplits.x * (1.0 - u_cascadeSplits.w);
        if (viewDistance > blendStart)
            visibility = mix(nearShadow, shadowTap(shadowPosition1, NoL, 1),
                saturate((viewDistance - blendStart) / max(u_cascadeSplits.x - blendStart, 1e-4)));
        else
            visibility = nearShadow;
    }
    else if (viewDistance < u_cascadeSplits.y)
    {
        float middleShadow = shadowTap(shadowPosition1, NoL, 1);
        float blendStart = u_cascadeSplits.y * (1.0 - u_cascadeSplits.w);
        if (viewDistance > blendStart)
            visibility = mix(middleShadow, shadowTap(shadowPosition2, NoL, 2),
                saturate((viewDistance - blendStart) / max(u_cascadeSplits.y - blendStart, 1e-4)));
        else
            visibility = middleShadow;
    }
    else if (viewDistance < u_cascadeSplits.z)
        visibility = shadowTap(shadowPosition2, NoL, 2);
    return visibility;
}

float atmosphereTexCoord(float x, float size) { return 0.5 / size + x * (1.0 - 1.0 / size); }

float localShadowTap(vec3 direction, float compareDepth, float encodedSlot)
{
    float visibility = 1.0;
    if (encodedSlot < 1.5)
        visibility = shadowCube(s_pointShadow0, vec4(direction, compareDepth));
    else
    {
        vec3 absoluteDirection = abs(direction);
        float face = 0.0;
        vec2 faceUv = vec2(0.0, 0.0);
        if (absoluteDirection.x >= absoluteDirection.y && absoluteDirection.x >= absoluteDirection.z)
        {
            if (direction.x >= 0.0) { face = 0.0; faceUv = vec2(-direction.z, direction.y) / absoluteDirection.x; }
            else { face = 1.0; faceUv = vec2(direction.z, direction.y) / absoluteDirection.x; }
        }
        else if (absoluteDirection.y >= absoluteDirection.z)
        {
            if (direction.y >= 0.0) { face = 2.0; faceUv = vec2(direction.x, -direction.z) / absoluteDirection.y; }
            else { face = 3.0; faceUv = vec2(direction.x, direction.z) / absoluteDirection.y; }
        }
        else
        {
            if (direction.z >= 0.0) { face = 4.0; faceUv = vec2(direction.x, direction.y) / absoluteDirection.z; }
            else { face = 5.0; faceUv = vec2(-direction.x, direction.y) / absoluteDirection.z; }
        }
        vec2 uv = vec2(faceUv.x * 0.5 + 0.5, 0.5 - faceUv.y * 0.5);
        float layer = (encodedSlot - 2.0) * 6.0 + face;
        visibility = shadow2DArray(s_pointShadow1, vec4(uv, layer, compareDepth)).x;
    }
    return visibility;
}

float pointShadowVisibility(vec3 fromLight, vec3 normal, float NoL, vec4 shadowParams,
    float sourceRadius, float contactHardening, vec4 shapeTangent, vec3 shapeNormal,
    float shapeHalfHeight)
{
    if (shadowParams.x < 0.5)
        return 1.0;
    float sine = sqrt(saturate(1.0 - NoL * NoL));
    float slope = min(sine / max(NoL, 0.20), 4.0);
    vec3 biasedFromLight = fromLight + normal * shadowParams.y * (1.0 + slope);
    float majorDistance = max(abs(biasedFromLight.x), max(abs(biasedFromLight.y), abs(biasedFromLight.z)));
    float nearPlane = shadowParams.z;
    float farPlane = shadowParams.w;
    float compareDepth = farPlane / (farPlane - nearPlane)
        - farPlane * nearPlane / ((farPlane - nearPlane) * max(majorDistance, nearPlane));
    vec3 direction = normalize(biasedFromLight);
    vec3 helper = abs(direction.y) < 0.99 ? vec3(0.0, 1.0, 0.0) : vec3(1.0, 0.0, 0.0);
    vec3 tangent = normalize(cross(helper, direction));
    vec3 bitangent = cross(direction, tangent);
    float angularRadius = u_pointShadowAtlasParams.x * u_pointShadowAtlasParams.y;
    if (sourceRadius > 0.0)
    {
        float penumbraRadius = min(sourceRadius / max(length(biasedFromLight), 0.25), 0.045);
        float filterRadiusX = angularRadius + penumbraRadius;
        float filterRadiusY = filterRadiusX;
        vec3 filterTangent = tangent;
        vec3 filterBitangent = bitangent;
        if (contactHardening > 0.5)
        {
            // Comparison samplers cannot return stored depth. Binary-search
            // the center ray in linear distance when it sees a blocker.
            // Offset search rays can mistake the receiver plane for a blocker
            // and create long false shadows on nearby floor pixels.
            float blocked = 1.0 - localShadowTap(direction, compareDepth, shadowParams.x);
            if (blocked > 0.5)
            {
                float nearDistance = nearPlane;
                float farDistance = majorDistance;
                for (int step = 0; step < 6; ++step)
                {
                    float middle = 0.5 * (nearDistance + farDistance);
                    float middleDepth = farPlane / (farPlane - nearPlane)
                        - farPlane * nearPlane / ((farPlane - nearPlane) * middle);
                    float lit = localShadowTap(direction, middleDepth, shadowParams.x);
                    if (lit > 0.5) nearDistance = middle;
                    else farDistance = middle;
                }
                float blockerDistance = max(0.5 * (nearDistance + farDistance), nearPlane);
                float spread = max(majorDistance - blockerDistance, 0.0)
                    / max(blockerDistance * majorDistance, 0.01);
                vec3 shapeBitangent = cross(shapeNormal, shapeTangent.xyz);
                vec3 projectedX = shapeTangent.xyz - direction * dot(shapeTangent.xyz, direction);
                vec3 projectedY = shapeBitangent - direction * dot(shapeBitangent, direction);
                float projectionX = length(projectedX);
                float projectionY = length(projectedY);
                if (projectionX > 1e-4 && projectionY > 1e-4)
                {
                    filterTangent = projectedX / projectionX;
                    filterBitangent = projectedY / projectionY;
                    filterRadiusX = angularRadius + min(shapeTangent.w * spread * projectionX, 0.12);
                    filterRadiusY = angularRadius + min(shapeHalfHeight * spread * projectionY, 0.12);
                }
            }
        }
        float softVisibility = 0.0;
        for (int tap = 0; tap < 25; ++tap)
        {
            float sampleIndex = float(tap) + 0.5;
            float diskRadius = sqrt(sampleIndex / 25.0);
            float diskAngle = sampleIndex * 2.39996323;
            vec2 disk = vec2(cos(diskAngle), sin(diskAngle)) * diskRadius;
            vec3 tapDirection = direction
                + filterTangent * disk.x * filterRadiusX
                + filterBitangent * disk.y * filterRadiusY;
            softVisibility += localShadowTap(tapDirection, compareDepth, shadowParams.x);
        }
        return softVisibility / 25.0;
    }
    float visibility = 0.0;
    for (int tap = 0; tap < 5; ++tap)
    {
        vec2 disk = tap == 0 ? vec2(0.0, 0.0)
            : (tap == 1 ? vec2(1.0, 0.0)
            : (tap == 2 ? vec2(-1.0, 0.0)
            : (tap == 3 ? vec2(0.0, 1.0) : vec2(0.0, -1.0))));
        vec3 tapDirection = direction + (tangent * disk.x + bitangent * disk.y) * angularRadius;
        visibility += localShadowTap(tapDirection, compareDepth, shadowParams.x);
    }
    return visibility * 0.2;
}

vec3 lightIesPattern(float encodedLayer, vec3 rayDirection, vec3 emissionDirection, vec3 up)
{
    if (encodedLayer < 0.5)
        return vec3_splat(1.0);
    vec3 right = normalize(cross(emissionDirection, up));
    float horizontal = atan2(dot(rayDirection, up), dot(rayDirection, right));
    float vertical = acos(clamp(dot(rayDirection, emissionDirection), -1.0, 1.0));
    vec2 uv = vec2(fract(horizontal / 6.2831853 + 1.0), vertical / 3.14159265);
    return texture2DArrayLod(s_localReflectionProbe,
        vec3(uv, encodedLayer - 1.0), 0.0).rgb;
}

vec3 lightCookiePattern(float encodedLayer, vec3 fromLight, vec3 emissionDirection,
    vec3 up, float outerCosine, float rotation)
{
    if (encodedLayer < 0.5)
        return vec3_splat(1.0);
    vec3 right = normalize(cross(emissionDirection, up));
    float axialDistance = max(dot(fromLight, emissionDirection), 1e-4);
    float tangentOuter = sqrt(max(1.0 - outerCosine * outerCosine, 0.0))
        / max(outerCosine, 1e-4);
    float halfExtent = max(axialDistance * tangentOuter, 1e-4);
    vec2 patternPosition = vec2(dot(fromLight, right), -dot(fromLight, up)) / halfExtent;
    float cosine = cos(rotation);
    float sine = sin(rotation);
    patternPosition = vec2(cosine * patternPosition.x - sine * patternPosition.y,
        sine * patternPosition.x + cosine * patternPosition.y);
    vec2 uv = patternPosition * 0.5 + 0.5;
    return texture2DArrayLod(s_localReflectionProbe,
        vec3(clamp(uv, vec2_splat(0.0), vec2_splat(1.0)), encodedLayer - 1.0), 0.0).rgb;
}

vec3 pointLightContribution(vec4 positionRadius, vec4 colorIntensity, vec4 shadowParams,
    vec4 patternParams, vec3 worldPosition,
    vec3 N, vec3 V, float NoV, vec3 f0, vec3 baseColor, float metallic, float roughness)
{
    vec3 toPoint = positionRadius.xyz - worldPosition;
    float distanceSquared = dot(toPoint, toPoint);
    float radiusSquared = positionRadius.w * positionRadius.w;
    if (radiusSquared <= 0.0 || distanceSquared >= radiusSquared)
        return vec3_splat(0.0);
    float pointDistance = sqrt(max(distanceSquared, 1e-6));
    vec3 pointL = toPoint / pointDistance;
    float pointNoL = saturate(dot(N, pointL));
    vec3 pointH = normalize(V + pointL);
    float pointNoH = saturate(dot(N, pointH));
    float pointVoH = saturate(dot(V, pointH));
    vec3 pointF = fresnelSchlick(pointVoH, f0);
    float pointD = distributionGGX(pointNoH, roughness);
    float pointG = geometrySchlickGGX(NoV, roughness)
        * geometrySchlickGGX(pointNoL, roughness);
    vec3 pointSpecular = pointD * pointG * pointF
        / max(4.0 * NoV * pointNoL, 1e-4);
    vec3 pointDiffuse = (1.0 - pointF) * (1.0 - metallic) * baseColor / 3.14159265;
    float rangeFalloff = saturate(1.0 - distanceSquared / radiusSquared);
    float attenuation = rangeFalloff * rangeFalloff / max(distanceSquared, 0.25);
    vec3 ies = lightIesPattern(patternParams.x, -pointL,
        vec3(0.0, -1.0, 0.0), vec3(0.0, 0.0, 1.0));
    return (pointDiffuse + pointSpecular) * colorIntensity.rgb * ies
        * colorIntensity.w * attenuation * pointNoL
        * pointShadowVisibility(worldPosition - positionRadius.xyz, N, pointNoL, shadowParams,
            0.0, 0.0, vec4_splat(0.0), vec3_splat(0.0), 0.0);
}

vec3 spotLightContribution(vec4 positionRadius, vec4 directionOuter, vec4 colorIntensity,
    vec4 spotParams, vec4 shadowParams, vec4 upPattern, vec3 worldPosition, vec3 N, vec3 V, float NoV,
    vec3 f0, vec3 baseColor, float metallic, float roughness)
{
    vec3 fromLight = worldPosition - positionRadius.xyz;
    float distanceSquared = dot(fromLight, fromLight);
    float radiusSquared = positionRadius.w * positionRadius.w;
    if (radiusSquared <= 0.0 || distanceSquared >= radiusSquared)
        return vec3_splat(0.0);
    float lightDistance = sqrt(max(distanceSquared, 1e-6));
    vec3 pointL = -fromLight / lightDistance;
    float coneCosine = dot(fromLight / lightDistance, directionOuter.xyz);
    float cone = smoothstep(directionOuter.w, spotParams.x, coneCosine);
    if (cone <= 0.0)
        return vec3_splat(0.0);
    float pointNoL = saturate(dot(N, pointL));
    vec3 pointH = normalize(V + pointL);
    float pointNoH = saturate(dot(N, pointH));
    float pointVoH = saturate(dot(V, pointH));
    vec3 pointF = fresnelSchlick(pointVoH, f0);
    float pointD = distributionGGX(pointNoH, roughness);
    float pointG = geometrySchlickGGX(NoV, roughness)
        * geometrySchlickGGX(pointNoL, roughness);
    vec3 pointSpecular = pointD * pointG * pointF
        / max(4.0 * NoV * pointNoL, 1e-4);
    vec3 pointDiffuse = (1.0 - pointF) * (1.0 - metallic) * baseColor / 3.14159265;
    float rangeFalloff = saturate(1.0 - distanceSquared / radiusSquared);
    float attenuation = rangeFalloff * rangeFalloff / max(distanceSquared, 0.25);
    vec3 pattern = lightCookiePattern(upPattern.w, fromLight, directionOuter.xyz,
        upPattern.xyz, directionOuter.w, spotParams.y)
        * lightIesPattern(spotParams.z, fromLight / lightDistance,
            directionOuter.xyz, upPattern.xyz);
    return (pointDiffuse + pointSpecular) * colorIntensity.rgb * pattern * colorIntensity.w
        * attenuation * pointNoL * cone * pointShadowVisibility(fromLight, N, pointNoL, shadowParams,
            0.0, 0.0, vec4_splat(0.0), vec3_splat(0.0), 0.0);
}

vec3 areaLightSampleContribution(vec3 samplePosition, vec3 emissionDirection,
    vec3 color, float intensity, float radiusSquared, vec3 worldPosition,
    vec3 N, vec3 V, float NoV, vec3 f0, vec3 baseColor, float metallic, float roughness)
{
    vec3 fromLight = worldPosition - samplePosition;
    float distanceSquared = dot(fromLight, fromLight);
    if (radiusSquared <= 0.0 || distanceSquared >= radiusSquared)
        return vec3_splat(0.0);
    float lightDistance = sqrt(max(distanceSquared, 1e-6));
    vec3 fromLightDirection = fromLight / lightDistance;
    float emitterFacing = saturate(dot(emissionDirection, fromLightDirection));
    if (emitterFacing <= 0.0)
        return vec3_splat(0.0);
    vec3 L = -fromLightDirection;
    float NoL = saturate(dot(N, L));
    vec3 H = normalize(V + L);
    float NoH = saturate(dot(N, H));
    float VoH = saturate(dot(V, H));
    vec3 F = fresnelSchlick(VoH, f0);
    float D = distributionGGX(NoH, roughness);
    float G = geometrySchlickGGX(NoV, roughness) * geometrySchlickGGX(NoL, roughness);
    vec3 sampleSpecular = D * G * F / max(4.0 * NoV * NoL, 1e-4);
    vec3 sampleDiffuse = (1.0 - F) * (1.0 - metallic) * baseColor / 3.14159265;
    float rangeFalloff = saturate(1.0 - distanceSquared / radiusSquared);
    float attenuation = rangeFalloff * rangeFalloff / max(distanceSquared, 0.25);
    return (sampleDiffuse + sampleSpecular) * color * intensity
        * attenuation * NoL * emitterFacing * 0.25;
}

vec3 areaLightContribution(vec4 positionRadius, vec4 directionWidth, vec4 upHeight,
    vec4 colorIntensity, vec4 shadowParams, vec3 worldPosition, vec3 N, vec3 V, float NoV,
    vec3 f0, vec3 baseColor, float metallic, float roughness)
{
    if (positionRadius.w <= 0.0)
        return vec3_splat(0.0);
    vec3 right = normalize(cross(directionWidth.xyz, upHeight.xyz));
    vec3 fromCenter = worldPosition - positionRadius.xyz;
    float horizontal = clamp(dot(fromCenter, right),
        -directionWidth.w * 0.5, directionWidth.w * 0.5);
    float vertical = clamp(dot(fromCenter, upHeight.xyz),
        -upHeight.w * 0.5, upHeight.w * 0.5);
    vec3 representativePoint = positionRadius.xyz
        + right * horizontal + upHeight.xyz * vertical;
    float radiusSquared = positionRadius.w * positionRadius.w;
    // The nearest point on the finite emitter preserves a rectangular core on
    // nearby receivers. The sample helper contains the former four-way weight.
    float centerNoL = saturate(dot(N, normalize(positionRadius.xyz - worldPosition)));
    float panelFilterRadius = 0.065 * length(vec2(directionWidth.w, upHeight.w));
    float visibility = pointShadowVisibility(worldPosition - positionRadius.xyz,
        N, centerNoL, shadowParams, panelFilterRadius, 0.0,
        vec4_splat(0.0), vec3_splat(0.0), 0.0);
    return areaLightSampleContribution(representativePoint, directionWidth.xyz,
        colorIntensity.rgb, colorIntensity.w, radiusSquared, worldPosition,
        N, V, NoV, f0, baseColor, metallic, roughness) * (4.0 * visibility);
}

vec3 emissiveMeshContribution(vec4 positionRadius, vec4 directionArea, vec4 radiance,
    vec4 shadowParams, vec4 tangentWidth, vec4 secondaryShadowParams, vec4 shadowOrigin,
    vec3 worldPosition, vec3 N, vec3 V, float NoV,
    vec3 f0, vec3 baseColor, float metallic, float roughness)
{
    float area = abs(directionArea.w);
    if (area <= 0.0 || positionRadius.w <= 0.0)
        return vec3_splat(0.0);
    vec3 emissionDirection = directionArea.xyz;
    if (directionArea.w < 0.0 && dot(emissionDirection, worldPosition - positionRadius.xyz) < 0.0)
        emissionDirection = -emissionDirection;
    float radiusSquared = positionRadius.w * positionRadius.w;
    // The sample helper has a four-way weight. Area/pi converts the material's
    // emitted radiance into an approximate finite-triangle irradiance.
    float sourceRadius = 0.5 * sqrt(area);
    float visibility;
    if (secondaryShadowParams.x > 0.5)
    {
        vec3 halfOffset = tangentWidth.xyz * shadowOrigin.w;
        vec4 halfTangentWidth = vec4(tangentWidth.xyz, tangentWidth.w * 0.5);
        vec3 firstRay = positionRadius.xyz - halfOffset - worldPosition;
        vec3 secondRay = positionRadius.xyz + halfOffset - worldPosition;
        float firstDistanceSquared = max(dot(firstRay, firstRay), 1e-6);
        float secondDistanceSquared = max(dot(secondRay, secondRay), 1e-6);
        vec3 firstDirection = firstRay * inversesqrt(firstDistanceSquared);
        vec3 secondDirection = secondRay * inversesqrt(secondDistanceSquared);
        float firstNoL = saturate(dot(N, firstDirection));
        float secondNoL = saturate(dot(N, secondDirection));
        float firstVisibility = pointShadowVisibility(-firstRay, N, firstNoL, shadowParams,
            sourceRadius, 1.0, halfTangentWidth, directionArea.xyz, radiance.w);
        float secondVisibility = pointShadowVisibility(-secondRay, N, secondNoL, secondaryShadowParams,
            sourceRadius, 1.0, halfTangentWidth, directionArea.xyz, radiance.w);
        float firstWeight = firstNoL * abs(dot(directionArea.xyz, firstDirection))
            / max(firstDistanceSquared, 0.25);
        float secondWeight = secondNoL * abs(dot(directionArea.xyz, secondDirection))
            / max(secondDistanceSquared, 0.25);
        visibility = (firstVisibility * firstWeight + secondVisibility * secondWeight)
            / max(firstWeight + secondWeight, 1e-5);
    }
    else
    {
        float shadowNoL = saturate(dot(N, normalize(shadowOrigin.xyz - worldPosition)));
        visibility = pointShadowVisibility(worldPosition - shadowOrigin.xyz,
            N, shadowNoL, shadowParams, sourceRadius, 1.0,
            tangentWidth, directionArea.xyz, radiance.w);
    }
    return areaLightSampleContribution(positionRadius.xyz, emissionDirection,
        radiance.rgb, area * (4.0 / 3.14159265), radiusSquared, worldPosition,
        N, V, NoV, f0, baseColor, metallic, roughness) * visibility;
}

void main()
{
    vec4 baseSample = u_textureFlags.x > 0.5 ? texture2D(s_baseColor, v_materialUv0.xy) : vec4_splat(1.0);
    vec4 base = baseSample * u_baseColorFactor;
    if (base.a < u_materialParams.w)
        discard;

    vec2 mr = vec2(u_materialParams.x, u_materialParams.y);
    if (u_textureFlags.y > 0.5)
    {
        vec4 packed = texture2D(s_metalRough, v_materialUv0.zw);
        mr *= vec2(packed.b, packed.g);
    }
    float metallic = saturate(mr.x);
    float roughness = clamp(mr.y, 0.045, 1.0);
    float materialAo = 1.0;
    if (u_materialOcclusion.x > 0.0)
        materialAo = 1.0 + u_materialOcclusion.x * (texture2D(s_occlusion, v_occlusionUv).r - 1.0);

    vec3 N = normalize(v_normal);
    if (u_textureFlags.z > 0.5 && u_materialParams.z != 0.0)
    {
        vec3 T = v_tangent.xyz - N * dot(N, v_tangent.xyz);
        // Interpolation and invalid/degenerate authored tangents must not feed
        // normalize(0), especially at UV seams or collapsed texture scales.
        if (dot(T, T) < 1e-12)
            T = cross(abs(N.z) < 0.9 ? vec3(0.0, 0.0, 1.0) : vec3(0.0, 1.0, 0.0), N);
        T = normalize(T);
        vec3 B = cross(N, T) * (v_tangent.w < 0.0 ? -1.0 : 1.0);
        vec3 mapN = texture2D(s_normal, v_materialUv1.xy).xyz * 2.0 - 1.0;
        mapN.xy *= u_materialParams.z;
        N = normalize(T * mapN.x + B * mapN.y + N * mapN.z);
    }

    vec3 V = normalize(u_cameraPos.xyz - v_worldPos);
    // Atmosphere provides the surface-to-sun direction.
    vec3 L = normalize(u_lightDirIntensity.xyz);
    vec3 H = normalize(V + L);
    float NoV = max(dot(N, V), 1e-4);
    float NoL = max(dot(N, L), 0.0);
    float NoH = max(dot(N, H), 0.0);
    float VoH = max(dot(V, H), 0.0);

    vec3 f0 = mix(vec3_splat(0.04), base.rgb, metallic);
    vec3 F = fresnelSchlick(VoH, f0);
    float D = distributionGGX(NoH, roughness);
    float G = geometrySchlickGGX(NoV, roughness) * geometrySchlickGGX(NoL, roughness);
    vec3 specular = D * G * F / max(4.0 * NoV * NoL, 1e-4);
    vec3 diffuse = (1.0 - F) * (1.0 - metallic) * base.rgb / 3.14159265;

    // Occluded direct sunlight contributes no diffuse or specular energy.
    float viewDepth = max(dot(v_worldPos - u_cameraPos.xyz, normalize(u_skyForward.xyz)), 0.0);
    float visibility = shadowVisibility(viewDepth, NoL,
        v_shadowPos0, v_shadowPos1, v_shadowPos2);
    vec3 direct = (diffuse + specular) * u_lightColor.rgb * u_lightDirIntensity.w * NoL * visibility;
    direct += pointLightContribution(u_pointLightPositionRadius0, u_pointLightColorIntensity0, u_pointLightShadow[0], u_pointLightPattern[0],
        v_worldPos, N, V, NoV, f0, base.rgb, metallic, roughness);
    direct += pointLightContribution(u_pointLightPositionRadius1, u_pointLightColorIntensity1, u_pointLightShadow[1], u_pointLightPattern[1],
        v_worldPos, N, V, NoV, f0, base.rgb, metallic, roughness);
    direct += pointLightContribution(u_pointLightPositionRadius2, u_pointLightColorIntensity2, u_pointLightShadow[2], u_pointLightPattern[2],
        v_worldPos, N, V, NoV, f0, base.rgb, metallic, roughness);
    direct += pointLightContribution(u_pointLightPositionRadius3, u_pointLightColorIntensity3, u_pointLightShadow[3], u_pointLightPattern[3],
        v_worldPos, N, V, NoV, f0, base.rgb, metallic, roughness);
    direct += spotLightContribution(u_spotLightPositionRadius[0], u_spotLightDirectionOuter[0],
        u_spotLightColorIntensity[0], u_spotLightParams[0], u_spotLightShadow[0], u_spotLightUpPattern[0],
        v_worldPos, N, V, NoV, f0, base.rgb, metallic, roughness);
    direct += spotLightContribution(u_spotLightPositionRadius[1], u_spotLightDirectionOuter[1],
        u_spotLightColorIntensity[1], u_spotLightParams[1], u_spotLightShadow[1], u_spotLightUpPattern[1],
        v_worldPos, N, V, NoV, f0, base.rgb, metallic, roughness);
    direct += spotLightContribution(u_spotLightPositionRadius[2], u_spotLightDirectionOuter[2],
        u_spotLightColorIntensity[2], u_spotLightParams[2], u_spotLightShadow[2], u_spotLightUpPattern[2],
        v_worldPos, N, V, NoV, f0, base.rgb, metallic, roughness);
    direct += spotLightContribution(u_spotLightPositionRadius[3], u_spotLightDirectionOuter[3],
        u_spotLightColorIntensity[3], u_spotLightParams[3], u_spotLightShadow[3], u_spotLightUpPattern[3],
        v_worldPos, N, V, NoV, f0, base.rgb, metallic, roughness);
    direct += areaLightContribution(u_areaLightPositionRadius[0], u_areaLightDirectionWidth[0],
        u_areaLightUpHeight[0], u_areaLightColorIntensity[0], u_areaLightShadow[0],
        v_worldPos, N, V, NoV, f0, base.rgb, metallic, roughness);
    direct += areaLightContribution(u_areaLightPositionRadius[1], u_areaLightDirectionWidth[1],
        u_areaLightUpHeight[1], u_areaLightColorIntensity[1], u_areaLightShadow[1],
        v_worldPos, N, V, NoV, f0, base.rgb, metallic, roughness);
    direct += emissiveMeshContribution(u_emissiveLightPositionRadius[0],
        u_emissiveLightDirectionArea[0], u_emissiveLightRadiance[0], u_emissiveLightShadow[0],
        u_emissiveLightTangentWidth[0], u_emissiveLightShadowSecondary[0], u_emissiveLightShadowOrigin[0],
        v_worldPos, N, V, NoV, f0, base.rgb, metallic, roughness);
    direct += emissiveMeshContribution(u_emissiveLightPositionRadius[1],
        u_emissiveLightDirectionArea[1], u_emissiveLightRadiance[1], u_emissiveLightShadow[1],
        u_emissiveLightTangentWidth[1], u_emissiveLightShadowSecondary[1], u_emissiveLightShadowOrigin[1],
        v_worldPos, N, V, NoV, f0, base.rgb, metallic, roughness);
    direct += emissiveMeshContribution(u_emissiveLightPositionRadius[2],
        u_emissiveLightDirectionArea[2], u_emissiveLightRadiance[2], u_emissiveLightShadow[2],
        u_emissiveLightTangentWidth[2], u_emissiveLightShadowSecondary[2], u_emissiveLightShadowOrigin[2],
        v_worldPos, N, V, NoV, f0, base.rgb, metallic, roughness);
    direct += emissiveMeshContribution(u_emissiveLightPositionRadius[3],
        u_emissiveLightDirectionArea[3], u_emissiveLightRadiance[3], u_emissiveLightShadow[3],
        u_emissiveLightTangentWidth[3], u_emissiveLightShadowSecondary[3], u_emissiveLightShadowOrigin[3],
        v_worldPos, N, V, NoV, f0, base.rgb, metallic, roughness);
    float hemi = N.y * 0.5 + 0.5;
    vec2 irradianceUv = vec2(atmosphereTexCoord(L.y * 0.5 + 0.5, 64.0),
        atmosphereTexCoord(saturate(u_atmosphereParams.z / 60.0), 16.0));
    vec3 physicalSkyIrradiance = u_environmentParams.w > 0.5
        ? hdriDiffuseIrradiance(N)
        : texture2D(s_environmentIrradiance, irradianceUv).rgb * u_environmentParams.x;
    // Keep a small gameplay floor for unresolved multi-bounce illumination;
    // view-dependent low-light adaptation is handled by the histogram pass.
    vec3 readableSkyIrradiance = u_environmentParams.w > 0.5 ? physicalSkyIrradiance
        : max(physicalSkyIrradiance, u_ambientSky.rgb * 0.85);
    vec3 ambientLight = u_environmentParams.w > 0.5 ? physicalSkyIrradiance
        : readableSkyIrradiance * hemi + u_ambientGround.rgb * (1.0 - hemi);
    // Captured walls replace exposed sky with their reflected radiance.
    // During capture the probe weight is zero, preventing recursive feedback.
    float localWeight0 = reflectionProbeWeight(v_worldPos, u_reflectionProbePosition[0],
        u_reflectionProbeMin[0], u_reflectionProbeMax[0]);
    float localWeight1 = reflectionProbeWeight(v_worldPos, u_reflectionProbePosition[1],
        u_reflectionProbeMin[1], u_reflectionProbeMax[1]);
    float localWeightSum = localWeight0 + localWeight1;
    float localTotalWeight = saturate(localWeightSum);
    if (localWeightSum > 0.0)
    {
        vec3 localIrradiance0 = localWeight0 > 0.0
            ? texture2DArray(s_localDiffuseProbe, cubeAtlasCoordinate(N, 0.0)).rgb
            : vec3_splat(0.0);
        vec3 localIrradiance1 = localWeight1 > 0.0
            ? texture2DArray(s_localDiffuseProbe, cubeAtlasCoordinate(N, 1.0)).rgb
            : vec3_splat(0.0);
        if (u_gridOrigin.w > 0.5)
        {
            vec4 gridMix = sampleGridLighting(v_worldPos, N);
            if (gridMix.a > 0.5)
                localIrradiance0 = readableSkyIrradiance * gridMix.x
                    + u_ambientGround.rgb * gridMix.y + localIrradiance0 * gridMix.z;
        }
        vec3 blendedLocalIrradiance = (localIrradiance0 * localWeight0
            + localIrradiance1 * localWeight1) / max(localWeightSum, 1e-4);
        ambientLight = mix(ambientLight, blendedLocalIrradiance, localTotalWeight);
    }
    vec3 ambient = ambientLight * base.rgb * (1.0 - metallic) / 3.14159265;
    vec3 reflection = reflect(-V, N);
    float environmentLod = roughness * u_environmentParams.z;
    vec3 globalEnvironment = textureCubeLod(s_environmentSpecular, reflection, environmentLod).rgb;
    vec3 localDirection0 = boxProjectedDirection(v_worldPos, reflection,
        u_reflectionProbePosition[0], u_reflectionProbeMin[0], u_reflectionProbeMax[0]);
    vec3 localDirection1 = boxProjectedDirection(v_worldPos, reflection,
        u_reflectionProbePosition[1], u_reflectionProbeMin[1], u_reflectionProbeMax[1]);
    vec3 localEnvironment0 = localWeight0 > 0.0
        ? texture2DArrayLod(s_localReflectionProbe,
            cubeAtlasCoordinate(localDirection0, 0.0), environmentLod).rgb
        : vec3_splat(0.0);
    vec3 localEnvironment1 = localWeight1 > 0.0
        ? texture2DArrayLod(s_localReflectionProbe,
            cubeAtlasCoordinate(localDirection1, 1.0), environmentLod).rgb
        : vec3_splat(0.0);
    vec3 blendedLocalEnvironment = (localEnvironment0 * localWeight0
        + localEnvironment1 * localWeight1) / max(localWeightSum, 1e-4);
    vec3 environment = mix(globalEnvironment, blendedLocalEnvironment, localTotalWeight)
        * u_environmentParams.y;
    // Solar specular is already evaluated by the shadowed direct GGX lobe.
    vec2 integratedBrdf = texture2D(s_brdfLut, vec2(NoV, roughness)).rg;
    vec3 environmentSpecular = environment * (f0 * integratedBrdf.x + integratedBrdf.y);
    vec3 emissive = u_emissiveFactor.rgb;
    if (u_textureFlags.w > 0.5)
        emissive *= texture2D(s_emissive, v_materialUv1.zw).rgb;

    // glTF AO attenuates indirect diffuse and specular only. Direct lights,
    // shadow maps, emissive radiance, and alpha are independent of material AO.
    vec3 hdr = max(direct + (ambient + environmentSpecular) * materialAo + emissive, vec3_splat(0.0));

    gl_FragData[0] = vec4(hdrForStorage(hdr), base.a);
    gl_FragData[1] = vec4(N * 0.5 + 0.5, materialAo);
    gl_FragData[2] = vec4(u_selectionId.x, 0.0, 0.0, 1.0);
}
