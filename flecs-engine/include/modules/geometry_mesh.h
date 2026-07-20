#ifndef FLECS_ENGINE_GEOMETRY_MESH_H
#define FLECS_ENGINE_GEOMETRY_MESH_H

#undef ECS_META_IMPL
#ifndef FLECS_ENGINE_GEOMETRY_MESH_IMPL
#define ECS_META_IMPL EXTERN
#endif

ECS_STRUCT(FlecsMesh3, {
    ecs_vec_t vertices;  /* vec<vec3> */
    ecs_vec_t normals;   /* vec<vec3> */
    ecs_vec_t uvs;       /* vec<vec2> */
    ecs_vec_t tangents;  /* vec<vec4> (xyz tangent + w bitangent sign) */
    ecs_vec_t indices;   /* vec<uint32> */
    ecs_vec_t colors;    /* vec<rgba>, optional per-vertex colors. Meshes with
                          * colors render through the vertex-color batch (add
                          * FlecsVertexColors to the instance): rgb multiplies
                          * albedo, alpha multiplies material roughness. */
    float shadow_sink;
});

/* Re-upload only the color buffer of an already-uploaded mesh. Cheap path for
 * meshes whose colors animate: no buffer recreation, no vertex/index/tangent
 * work. Falls back to a full re-upload when the color count changed. */
void flecsEngine_mesh3_updateColors(
    ecs_world_t *world,
    ecs_entity_t mesh_entity);

void flecsEngine_mesh3_updateColorRange(
    ecs_world_t *world,
    ecs_entity_t mesh_entity,
    int32_t first,
    int32_t count);

void flecsEngine_mesh3_updateVertices(
    ecs_world_t *world,
    ecs_entity_t mesh_entity);

bool flecsEngine_objectWorldAABB(
    const ecs_world_t *world,
    ecs_entity_t entity,
    FlecsAABB *out);

#endif
