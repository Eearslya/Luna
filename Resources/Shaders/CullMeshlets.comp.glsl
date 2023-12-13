#version 460 core
#extension GL_EXT_shader_atomic_float : require
#extension GL_EXT_scalar_block_layout : require

#include "Common.glsli"
#include "VisBuffer.glsli"

//layout(local_size_x = 128) in;

struct DispatchIndirectCommand {
  uint x;
  uint y;
  uint z;
};

layout(push_constant) uniform CullPushConstants {
  uint BatchID;
  uint MeshletsPerBatch;
};

layout(set = 0, binding = 0, scalar) uniform SceneBuffer {
  SceneData Scene;
};

layout(set = 1, binding = 0) uniform ComputeUniforms {
  uint MeshletCount;
} Uniforms;

layout(set = 1, binding = 1, scalar) restrict buffer CullTriangleDispatchBuffer {
  DispatchIndirectCommand Commands[];
} CullTriangleDispatch;
layout(set = 1, binding = 2, scalar) restrict writeonly buffer VisibleMeshletIDs {
  uint Indices[];
} VisibleMeshlets;
layout(set = 1, binding = 3, scalar) restrict readonly buffer MeshletBuffer {
  Meshlet Meshlets[];
};
layout(set = 1, binding = 4, scalar) restrict readonly buffer TransformBuffer {
  mat4 Transforms[];
};

bool IsAABBInsidePlane(vec3 center, vec3 extent, vec4 plane) {
  const vec3 normal = plane.xyz;
  const float radius = dot(extent, abs(normal));

  return (dot(normal, center) - plane.w) >= -radius;
}

bool CullMeshletFrustum(uint meshletId) {
  const uint instanceId = Meshlets[meshletId].InstanceID;
  const mat4 transform = Transforms[instanceId];
  const vec3 aabbMin = Meshlets[meshletId].AABBMin.xyz;
  const vec3 aabbMax = Meshlets[meshletId].AABBMax.xyz;
  const vec3 aabbCenter = (aabbMin + aabbMax) / 2.0;
  const vec3 aabbExtent = aabbMax - aabbCenter;
  const vec3 worldAABBCenter = vec3(transform * vec4(aabbCenter, 1.0));
  const vec3 right = vec3(transform[0]) * aabbExtent.x;
  const vec3 up = vec3(transform[1]) * aabbExtent.y;
  const vec3 forward = vec3(-transform[2]) * aabbExtent.z;

  const vec3 worldExtent = vec3(
    abs(dot(vec3(1, 0, 0), right)) +
    abs(dot(vec3(1, 0, 0), up)) +
    abs(dot(vec3(1, 0, 0), forward)),
    abs(dot(vec3(0, 1, 0), right)) +
    abs(dot(vec3(0, 1, 0), up)) +
    abs(dot(vec3(0, 1, 0), forward)),
    abs(dot(vec3(0, 0, 1), right)) +
    abs(dot(vec3(0, 0, 1), up)) +
    abs(dot(vec3(0, 0, 1), forward))
  );
  for (uint i = 0; i < 6; ++i) {
    if (!IsAABBInsidePlane(worldAABBCenter, worldExtent, Scene.FrustumPlanes[i])) { return false; }
  }

  return true;
}

void main() {
  const uint meshletId = gl_GlobalInvocationID.x + (BatchID * MeshletsPerBatch);
  if (meshletId >= Uniforms.MeshletCount) { return; }

  bool isVisible = true;
  isVisible = CullMeshletFrustum(meshletId);
  if (isVisible) {
    const uint index = atomicAdd(CullTriangleDispatch.Commands[BatchID].x, 1);
    VisibleMeshlets.Indices[index + (BatchID * MeshletsPerBatch)] = meshletId;
  }
}
