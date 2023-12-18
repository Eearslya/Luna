#version 460 core
#extension GL_EXT_shader_atomic_float : require
#extension GL_EXT_scalar_block_layout : require

#include "Common.glsli"
#include "VisBuffer.glsli"

struct GetMeshletUvBoundsParams {
  uint MeshletID;
  mat4 ViewProj;
  bool ClampNDC;
  bool ReverseZ;
};

layout(local_size_x = 64) in;

layout(set = 0, binding = 0, scalar) uniform SceneBuffer {
  SceneData Scene;
};

layout(set = 1, binding = 0) uniform ComputeUniforms {
  uint MeshletCount;
  uint IndicesPerBatch;
} Uniforms;
layout(set = 1, binding = 1, scalar) restrict readonly buffer MeshletBuffer {
  Meshlet Meshlets[];
};
layout(set = 1, binding = 2, scalar) restrict readonly buffer ScenePositions {
  vec3 Positions[];
};
layout(set = 1, binding = 3, scalar) restrict readonly buffer SceneAttributes {
  Vertex Attributes[];
};
layout(set = 1, binding = 4, scalar) restrict readonly buffer SceneIndices {
  uint Indices[];
};
layout(set = 1, binding = 5, scalar) restrict readonly buffer SceneTriangles {
  uint8_t Triangles[];
};
layout(set = 1, binding = 6, scalar) restrict readonly buffer TransformBuffer {
  mat4 Transforms[];
};
layout(set = 1, binding = 7) uniform sampler2D HZB;

layout(set = 1, binding = 8, scalar) restrict writeonly buffer DrawIndirect {
  DrawIndexedIndirectCommand DrawCommands[];
};
layout(set = 1, binding = 9, scalar) restrict writeonly buffer MeshletIndicesBuffer {
  uint MeshletIndices[];
};
layout(set = 1, binding = 10, scalar) restrict buffer VisBufferStatsBuffer {
  VisBufferStats Stats;
};

shared bool sIsVisible;
shared uint sBaseIndex;
shared uint sPrimitivesPassed;
shared mat4 sMVP;

void GetMeshletUvBounds(GetMeshletUvBoundsParams params, out vec2 minXY, out vec2 maxXY, out float nearestZ, out bool intersectsNearPlane) {
  uint meshletId = params.MeshletID;
  uint instanceId = Meshlets[meshletId].InstanceID;
  mat4 transform = Transforms[instanceId];
  vec3 aabbMin = Meshlets[meshletId].AABBMin;
  vec3 aabbMax = Meshlets[meshletId].AABBMax;
  vec4 aabbSize = vec4(aabbMax - aabbMin, 0.0);
  vec3[] aabbCorners = vec3[](
    aabbMin,
    aabbMin + aabbSize.xww,
    aabbMin + aabbSize.wyw,
    aabbMin + aabbSize.wwz,
    aabbMin + aabbSize.xyw,
    aabbMin + aabbSize.wyz,
    aabbMin + aabbSize.xwz,
    aabbMin + aabbSize.xyz
  );
  nearestZ = 0;

  minXY = vec2(1e20);
  maxXY = vec2(-1e20);
  mat4 mvp = params.ViewProj * transform;
  for (uint i = 0; i < 8; ++i) {
    vec4 clip = mvp * vec4(aabbCorners[i], 1.0);

    if (clip.w <= 0) {
      intersectsNearPlane = true;
      return;
    }

    clip.z = max(clip.z, 0.0);
    clip /= clip.w;
    if (params.ClampNDC) {
      clip.xy = clamp(clip.xy, -1.0, 1.0);
    }
    clip.xy = clip.xy * 0.5 + 0.5;
    minXY = min(minXY, clip.xy);
    maxXY = max(maxXY, clip.xy);
    nearestZ = clamp(max(nearestZ, clip.z), 0.0, 1.0);
  }

  intersectsNearPlane = false;
}

bool IsAABBInsidePlane(vec3 center, vec3 extent, vec4 plane) {
  vec3 normal = plane.xyz;
  float radius = dot(extent, abs(normal));

  return (dot(normal, center) - plane.w) >= -radius;
}

bool CullMeshletFrustum(uint meshletId) {
  uint instanceId = Meshlets[meshletId].InstanceID;
  mat4 transform = Transforms[instanceId];
  vec3 aabbMin = Meshlets[meshletId].AABBMin;
  vec3 aabbMax = Meshlets[meshletId].AABBMax;
  vec3 aabbCenter = (aabbMin + aabbMax) / 2.0;
  vec3 aabbExtent = aabbMax - aabbCenter;
  vec3 worldAABBCenter = vec3(transform * vec4(aabbCenter, 1.0));
  vec3 right = vec3(transform[0]) * aabbExtent.x;
  vec3 up = vec3(transform[1]) * aabbExtent.y;
  vec3 forward = vec3(-transform[2]) * aabbExtent.z;

  vec3 worldExtent = vec3(
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

bool CullSmallTriangle(vec2 vertices[3]) {
  const uint SubpixelBits = 8;
  const uint SubpixelMask = 0xff;
  const uint SubpixelSamples = 1 << SubpixelBits;

  ivec2 minBB = ivec2(1 << 30, 1 << 30);
  ivec2 maxBB = ivec2(-(1 << 30), -(1 << 30));

  for (uint i = 0; i < 3; ++i) {
    vec2 screenSpacePositionFP = vertices[i].xy * Scene.ViewportExtent;
    if (screenSpacePositionFP.x < -(1 << 23) || screenSpacePositionFP.x > (1 << 23) || screenSpacePositionFP.y < -(1 << 23) || screenSpacePositionFP.y > (1 << 23)) { return true; }

    ivec2 screenSpacePosition = ivec2(screenSpacePositionFP * SubpixelSamples);
    minBB = min(screenSpacePosition, minBB);
    maxBB = max(screenSpacePosition, maxBB);
  }

  return !(
    (
      ((minBB.x & SubpixelMask) > SubpixelSamples / 2)
   && ((maxBB.x - ((minBB.x & ~SubpixelMask) + SubpixelSamples / 2)) < (SubpixelSamples - 1)))
   || (
      ((minBB.y & SubpixelMask) > SubpixelSamples / 2)
   && ((maxBB.y - ((minBB.y & ~SubpixelMask) + SubpixelSamples / 2)) < (SubpixelSamples - 1))));

  return true;
}

bool CullQuadHiZ(vec2 minXY, vec2 maxXY, float nearestZ) {
  vec4 boxUvs = vec4(minXY, maxXY);
  boxUvs.y = 1.0 - boxUvs.y;
  boxUvs.w = 1.0 - boxUvs.w;
  vec2 hzbSize = vec2(textureSize(HZB, 0));
  float width = (boxUvs.z - boxUvs.x) * hzbSize.x;
  float height = (boxUvs.w - boxUvs.y) * hzbSize.y;

  float level = ceil(log2(max(width, height)));
  float[4] depths = float[](
    textureLod(HZB, boxUvs.xy, level).r,
    textureLod(HZB, boxUvs.zy, level).r,
    textureLod(HZB, boxUvs.xw, level).r,
    textureLod(HZB, boxUvs.zw, level).r
  );
  float depth = min(min(min(depths[0], depths[1]), depths[2]), depths[3]);

  if (nearestZ < depth) { return false; }

  return true;
}

bool CullTriangle(uint meshletId, uint localId) {
  uint primitiveId = localId * 3;

  uint vertexOffset   = Meshlets[meshletId].VertexOffset;
  uint indexOffset    = Meshlets[meshletId].IndexOffset;
  uint triangleOffset = Meshlets[meshletId].TriangleOffset;

  uint triangle0 = uint(Triangles[triangleOffset + primitiveId + 0]);
  uint triangle1 = uint(Triangles[triangleOffset + primitiveId + 1]);
  uint triangle2 = uint(Triangles[triangleOffset + primitiveId + 2]);

  uint index0 = Indices[indexOffset + triangle0];
  uint index1 = Indices[indexOffset + triangle1];
  uint index2 = Indices[indexOffset + triangle2];

  vec3 position0 = Positions[vertexOffset + index0];
  vec3 position1 = Positions[vertexOffset + index1];
  vec3 position2 = Positions[vertexOffset + index2];

  Vertex vertex0 = Attributes[vertexOffset + index0];
  Vertex vertex1 = Attributes[vertexOffset + index1];
  Vertex vertex2 = Attributes[vertexOffset + index2];

  vec4 posClip0 = sMVP * vec4(position0, 1.0);
  vec4 posClip1 = sMVP * vec4(position1, 1.0);
  vec4 posClip2 = sMVP * vec4(position2, 1.0);

  float det = determinant(mat3(posClip0.xyw, posClip1.xyw, posClip2.xyw));
  if (det <= 0) { return false; }

  vec3 posNdc0 = posClip0.xyz / posClip0.w;
  vec3 posNdc1 = posClip1.xyz / posClip1.w;
  vec3 posNdc2 = posClip2.xyz / posClip2.w;

  vec2 bboxNdcMin = min(posNdc0.xy, min(posNdc1.xy, posNdc2.xy));
  vec2 bboxNdcMax = max(posNdc0.xy, max(posNdc1.xy, posNdc2.xy));

  bool allBehind = posNdc0.z < 0 && posNdc1.z < 0 && posNdc2.z < 0;
  if (allBehind) { return false; }

  bool anyBehind = posNdc0.z < 0 || posNdc1.z < 0 || posNdc2.z < 0;
  if (anyBehind) { return true; }

  if (!RectIntersectRect(bboxNdcMin, bboxNdcMax, vec2(-1.0), vec2(1.0))) { return false; }

  vec2 posUv0 = posNdc0.xy * 0.5 + 0.5;
  vec2 posUv1 = posNdc1.xy * 0.5 + 0.5;
  vec2 posUv2 = posNdc2.xy * 0.5 + 0.5;
  if (!CullSmallTriangle(vec2[3](posUv0, posUv1, posUv2))) { return false; }

  return true;
}

void main() {
  uint meshletsPerBatch = gl_NumWorkGroups.x;
  uint batchId = gl_GlobalInvocationID.y;
  uint meshletOffset = batchId * meshletsPerBatch;
  uint meshletId = gl_WorkGroupID.x + meshletOffset;
  if (meshletId >= Uniforms.MeshletCount) { return; }

  uint localId = gl_LocalInvocationID.x;

  if (localId == 0) {
    sIsVisible = false;
    if (CullMeshletFrustum(meshletId)) {
      GetMeshletUvBoundsParams params;
      params.MeshletID = meshletId;
      params.ViewProj = Scene.ViewProjection;
      params.ClampNDC = true;
      params.ReverseZ = true;

      vec2 minXY;
      vec2 maxXY;
      float nearestZ;
      bool intersectsNearPlane;
      GetMeshletUvBounds(params, minXY, maxXY, nearestZ, intersectsNearPlane);
	  sIsVisible = intersectsNearPlane;
      if (!sIsVisible) {
        sIsVisible = CullQuadHiZ(minXY, maxXY, nearestZ + 0.0001);
      }
    }
  }

  barrier();

  if (!sIsVisible) { return; }

  if (localId == 0) {
    uint instanceId = Meshlets[meshletId].InstanceID;
    atomicAdd(Stats.VisibleMeshlets, 1);
    sPrimitivesPassed = 0;
    sMVP = Scene.ViewProjection * Transforms[instanceId];
  }

  barrier();

  uint triangleCount = Meshlets[meshletId].TriangleCount;
  bool primitivePassed = false;
  uint activePrimitiveId = 0;
  if (localId < triangleCount) {
    primitivePassed = CullTriangle(meshletId, localId);
    if (primitivePassed) {
      activePrimitiveId = atomicAdd(sPrimitivesPassed, 1);
    }
  }

  barrier();

  if (localId == 0) {
    sBaseIndex = atomicAdd(DrawCommands[batchId].IndexCount, sPrimitivesPassed * 3);
    atomicAdd(Stats.VisibleTriangles, sPrimitivesPassed);
  }

  barrier();

  if (primitivePassed) {
    uint triangleId = localId * 3;
    uint indexOffset = sBaseIndex + (batchId * Uniforms.IndicesPerBatch) + activePrimitiveId * 3;
    MeshletIndices[indexOffset + 0] = (meshletId << MeshletPrimitiveBits) | ((triangleId + 0) & MeshletPrimitiveMask);
    MeshletIndices[indexOffset + 1] = (meshletId << MeshletPrimitiveBits) | ((triangleId + 1) & MeshletPrimitiveMask);
    MeshletIndices[indexOffset + 2] = (meshletId << MeshletPrimitiveBits) | ((triangleId + 2) & MeshletPrimitiveMask);
  }
}
