#version 460 core
#extension GL_EXT_scalar_block_layout : require

#include "Common.glsli"
#include "VisBuffer.glsli"

layout(set = 0, binding = 0, scalar) uniform SceneBuffer {
  SceneData Scene;
};

layout(set = 1, binding = 1, scalar) restrict readonly buffer MeshletBuffer {
  Meshlet Meshlets[];
};
layout(set = 1, binding = 2, scalar) readonly buffer VertexPositions {
  vec3 Positions[];
};
layout(set = 1, binding = 3, scalar) readonly buffer VertexAttributes {
  Vertex Attributes[];
};
layout(set = 1, binding = 4, scalar) readonly buffer MeshletIndices {
  uint Indices[];
};
layout(set = 1, binding = 5, scalar) readonly buffer MeshletPrimitives {
  uint8_t Triangles[];
};
layout(set = 1, binding = 6, scalar) readonly buffer TransformBuffer {
  mat4 Transforms[];
};

layout(location = 0) flat out uint outMeshlet;
layout(location = 1) out vec3 outNormal;

void main() {
  const uint meshletId = (uint(gl_VertexIndex) >> MeshletPrimitiveBits) & MeshletIdMask;
  const uint primitiveId = uint(gl_VertexIndex) & MeshletPrimitiveMask;

  const uint vertexOffset   = Meshlets[meshletId].VertexOffset;
  const uint indexOffset    = Meshlets[meshletId].IndexOffset;
  const uint triangleOffset = Meshlets[meshletId].TriangleOffset;
  const uint instanceId     = Meshlets[meshletId].InstanceID;

  const uint primitive = uint(Triangles[triangleOffset + primitiveId]);
  const uint index = Indices[indexOffset + primitive];
  const vec3 position = Positions[vertexOffset + index];
  const Vertex vertex = Attributes[vertexOffset + index];
  const mat4 transform = Transforms[instanceId];

  outMeshlet = meshletId;
  outNormal = vertex.Normal;
  gl_Position = Scene.ViewProjection * transform * vec4(position, 1.0);
}
