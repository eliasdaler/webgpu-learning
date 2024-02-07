#pragma once

#include <string>

// Will move them all to files... eventually

namespace
{

const char* meshShadersCommonDefs = R"(
struct PerFrameData {
    viewProj: mat4x4f,
    invViewProj: mat4x4f,
    cameraPos: vec4f,
    pixelSize: vec2f,
};

struct DirectionalLight {
    directionAndMisc: vec4f,
    colorAndIntensity: vec4f,
};

struct CSMData {
    cascadeFarPlaneZs: vec4f,
    lightSpaceTMs: array<mat4x4f, 4>,
};

struct MeshData {
    model: mat4x4f,
};

struct MaterialData {
    baseColor: vec4f,
};

struct VertexOutput {
    @builtin(position) position: vec4f,
    @location(0) pos: vec3f,
    @location(1) normal: vec3f,
    @location(2) uv: vec2f,
};
)";

const std::string meshDrawDepthOnlyVertexShaderSource = std::string(meshShadersCommonDefs) + R"(
@group(0) @binding(0) var<uniform> fd: PerFrameData;

@group(1) @binding(0) var<uniform> meshData: MeshData;
@group(1) @binding(1) var<storage, read> jointMatrices: array<mat4x4f>;

// mesh attributes
@group(1) @binding(2) var<storage, read> positions: array<vec4f>;
@group(1) @binding(3) var<storage, read> normals: array<vec4f>;
@group(1) @binding(4) var<storage, read> tangents: array<vec4f>;
@group(1) @binding(5) var<storage, read> uvs: array<vec2f>;
// skinned meshes only
@group(1) @binding(6) var<storage, read> jointIds: array<vec4u>;
@group(1) @binding(7) var<storage, read> weights: array<vec4f>;

fn calculateWorldPos(vertexIndex: u32, pos: vec4f) -> vec4f {
    // FIXME: pass whether or not mesh has skeleton via other means,
    // otherwise this won't work for meshes with four joints.
    let hasSkeleton = (arrayLength(&jointIds) != 4);
    if (!hasSkeleton) {
        return meshData.model * pos;
    }

    let jointIds = jointIds[vertexIndex];
    let weights = weights[vertexIndex];
    let skinMatrix =
        weights.x * jointMatrices[jointIds.x] +
        weights.y * jointMatrices[jointIds.y] +
        weights.z * jointMatrices[jointIds.z] +
        weights.w * jointMatrices[jointIds.w];
    return meshData.model * skinMatrix * pos;
}

@vertex
fn vs_main(@builtin(vertex_index) vertexIndex: u32) -> VertexOutput {
    let pos = positions[vertexIndex];
    let normal = normals[vertexIndex];
    // let tangent = tangents[vertexIndex]; // unused for now
    let uv = uvs[vertexIndex];

    let worldPos = calculateWorldPos(vertexIndex, pos);

    var out: VertexOutput;
    out.position = fd.viewProj * worldPos;
    out.pos = worldPos.xyz;
    out.normal = normal.xyz;
    out.uv = uv;

    return out;
}
)";

const std::string meshDrawVertexShaderSource = std::string(meshShadersCommonDefs) + R"(
@group(0) @binding(0) var<uniform> fd: PerFrameData;
@group(0) @binding(1) var<uniform> dirLight: DirectionalLight;
@group(0) @binding(2) var<uniform> csmData: CSMData;

@group(2) @binding(0) var<uniform> meshData: MeshData;
@group(2) @binding(1) var<storage, read> jointMatrices: array<mat4x4f>;

// mesh attributes
@group(2) @binding(2) var<storage, read> positions: array<vec4f>;
@group(2) @binding(3) var<storage, read> normals: array<vec4f>;
@group(2) @binding(4) var<storage, read> tangents: array<vec4f>;
@group(2) @binding(5) var<storage, read> uvs: array<vec2f>;
// skinned meshes only
@group(2) @binding(6) var<storage, read> jointIds: array<vec4u>;
@group(2) @binding(7) var<storage, read> weights: array<vec4f>;

fn calculateWorldPos(vertexIndex: u32, pos: vec4f) -> vec4f {
    // FIXME: pass whether or not mesh has skeleton via other means,
    // otherwise this won't work for meshes with four joints.
    let hasSkeleton = (arrayLength(&jointIds) != 4);
    if (!hasSkeleton) {
        return meshData.model * pos;
    }

    let jointIds = jointIds[vertexIndex];
    let weights = weights[vertexIndex];
    let skinMatrix =
        weights.x * jointMatrices[jointIds.x] +
        weights.y * jointMatrices[jointIds.y] +
        weights.z * jointMatrices[jointIds.z] +
        weights.w * jointMatrices[jointIds.w];
    return meshData.model * skinMatrix * pos;
}

@vertex
fn vs_main(@builtin(vertex_index) vertexIndex: u32) -> VertexOutput {
    let pos = positions[vertexIndex];
    let normal = normals[vertexIndex];
    // let tangent = tangents[vertexIndex]; // unused for now
    let uv = uvs[vertexIndex];

    let worldPos = calculateWorldPos(vertexIndex, pos);

    var out: VertexOutput;
    out.position = fd.viewProj * worldPos;
    out.pos = worldPos.xyz;
    out.normal = normal.xyz;
    out.uv = uv;

    return out;
}
)";

const std::string meshDrawFragmentShaderSource = std::string(meshShadersCommonDefs) + R"(
@group(0) @binding(0) var<uniform> fd: PerFrameData;
@group(0) @binding(1) var<uniform> dirLight: DirectionalLight;
@group(0) @binding(2) var<uniform> csmData: CSMData;

@group(1) @binding(0) var<uniform> md: MaterialData;
@group(1) @binding(1) var texture: texture_2d<f32>;
@group(1) @binding(2) var texSampler: sampler;

fn calculateSpecularBP(NoH: f32) -> f32 {
    let shininess = 32.0 * 4.0;
    return pow(NoH, shininess);
}

fn blinnPhongBRDF(diffuse: vec3f, n: vec3f, v: vec3f, l: vec3f, h: vec3f) -> vec3f {
    let Fd = diffuse;

    // specular
    // TODO: read from spec texture / pass spec param
    let specularColor = diffuse * 0.5;
    let NoH = saturate(dot(n, h));
    let Fr = specularColor * calculateSpecularBP(NoH);

    return Fd + Fr;
}

@fragment
fn fs_main(in: VertexOutput) -> @location(0) vec4f {
    let diffuse = md.baseColor.rgb * textureSample(texture, texSampler, in.uv).rgb;

    let ambient = vec3(0.05, 0.05, 0.05);

    let lightDir = -dirLight.directionAndMisc.xyz;
    let lightColor = dirLight.colorAndIntensity.rgb;

    let viewPos = fd.cameraPos.xyz;

    let n = normalize(in.normal);
    let l = normalize(lightDir - in.pos);
    let v = normalize(viewPos - in.pos);
    let h = normalize(v + l);

    let fr = blinnPhongBRDF(diffuse, n, v, l, h);

    let NoL = saturate(dot(n, l));
    var fragColor = fr * lightColor * NoL;

    // ambient
    fragColor += diffuse * ambient;

    return vec4f(fragColor, 1.0);
}
)";

const char* spriteShaderSource = R"(
struct SpriteVertex {
    positionAndUV: vec4f,
};

struct VertexOutput {
    @builtin(position) position: vec4f,
    @location(0) uv: vec2f,
};

@group(0) @binding(0) var<storage, read> vertices: array<SpriteVertex>;
@group(0) @binding(1) var texture: texture_2d<f32>;
@group(0) @binding(2) var texSampler: sampler;

@vertex
fn vs_main(
    @builtin(vertex_index) vertexIndex : u32) -> VertexOutput {
    let vertex = vertices[vertexIndex];

    var out: VertexOutput;
    out.position = vec4f(vertex.positionAndUV.xy, 0.0, 1.0);
    out.uv = vertex.positionAndUV.zw;
    return out;
}

@fragment
fn fs_main(in: VertexOutput) -> @location(0) vec4f {
    let textureColor = textureSample(texture, texSampler, in.uv).rgba;
    if (textureColor.a < 0.01) {
        discard;
    }
    return vec4(textureColor.rgb, 1.0);
}
)";

const char* fullscreenTriangleShaderSource = R"(
struct VSOutput {
  @builtin(position) position: vec4f,
  @location(0) uv: vec2f,
};

@vertex
fn vs_main(@builtin(vertex_index) vertexIndex : u32) -> VSOutput {
    let pos = array(
        vec2f(-1.0, -1.0),
        vec2f(3.0, -1.0),
        vec2f(-1.0, 3.0f),
    );
    let uv = array(
        vec2f(0, 1),
        vec2f(2, 1),
        vec2f(0, -1),
    );

    var vsOutput: VSOutput;
    vsOutput.position = vec4(pos[vertexIndex], 0.0, 1.0);
    vsOutput.uv = uv[vertexIndex];
    return vsOutput;
}
)";

const char* skyboxShaderSource = R"(
struct VSOutput {
  @builtin(position) position: vec4f,
  @location(0) uv: vec2f,
};

struct PerFrameData {
    viewProj: mat4x4f,
    invViewProj: mat4x4f,
    cameraPos: vec4f,
    pixelSize: vec2f,
};

@group(0) @binding(0) var<uniform> fd: PerFrameData;
@group(0) @binding(1) var texture: texture_cube<f32>;
@group(0) @binding(2) var texSampler: sampler;

@fragment
fn fs_main(fsInput: VSOutput) -> @location(0) vec4f {
    let uv = fsInput.position.xy * fd.pixelSize;
    var ndc = vec2(
        uv.x * 2.0 - 1.0,
        1.0 - 2.0 * uv.y);

    let coord = fd.invViewProj * vec4(ndc, 1.0, 1.0);
    let samplePoint = normalize(coord.xyz / vec3(coord.w) - fd.cameraPos.xyz);

    let textureColor = textureSample(texture, texSampler, samplePoint);
    return vec4(textureColor.rgb, 1.0);
}
)";

const char* postFXShaderSource = R"(
struct VSOutput {
  @builtin(position) position: vec4f,
  @location(0) uv: vec2f,
};

struct PerFrameData {
    viewProj: mat4x4f,
    invViewProj: mat4x4f,
    cameraPos: vec4f,
    pixelSize: vec2f,
};

@group(0) @binding(0) var<uniform> fd: PerFrameData;
@group(0) @binding(1) var texture: texture_2d<f32>;
@group(0) @binding(2) var texSampler: sampler;

@fragment
fn fs_main(fsInput: VSOutput) -> @location(0) vec4f {
    let uv = fsInput.position.xy * fd.pixelSize;
    let fragColor = textureSample(texture, texSampler, uv);

    // gamma correction
    var color = pow(fragColor.rgb, vec3(1/2.2f));

    return vec4(color.rgb, 1.0);
}
)";

} // end of anonymous namespace
