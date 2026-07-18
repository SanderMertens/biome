#include <flecs_engine_test.h>

#include <stdio.h>
#include <string.h>

static const char* Transform3_fixture(void) {
    static char source_relative[1024];
    static const char *candidates[] = {
        "flecs-engine/test/etc/primitive.gltf",
        "test/etc/primitive.gltf",
        "etc/primitive.gltf"
    };

    for (int32_t i = 0; i < 3; i ++) {
        FILE *file = fopen(candidates[i], "rb");
        if (file) {
            fclose(file);
            return candidates[i];
        }
    }

    const char *source = __FILE__;
    const char *separator = strrchr(source, '/');
#ifdef _WIN32
    const char *backslash = strrchr(source, '\\');
    if (!separator || (backslash && backslash > separator)) {
        separator = backslash;
    }
#endif
    if (separator) {
        size_t directory_length = (size_t)(separator - source);
        if (directory_length + sizeof("/../etc/primitive.gltf") <=
            sizeof(source_relative))
        {
            memcpy(source_relative, source, directory_length);
            strcpy(source_relative + directory_length,
                "/../etc/primitive.gltf");
            FILE *file = fopen(source_relative, "rb");
            if (file) {
                fclose(file);
                return source_relative;
            }
        }
    }

    return NULL;
}

static ecs_entity_t Transform3_only_child(
    ecs_world_t *world,
    ecs_entity_t parent)
{
    ecs_entity_t result = 0;
    int32_t count = 0;
    ecs_iter_t it = ecs_children(world, parent);
    while (ecs_children_next(&it)) {
        for (int32_t i = 0; i < it.count; i ++) {
            result = it.entities[i];
            count ++;
        }
    }
    test_int(count, 1);
    return result;
}

void Transform3_child_world_transform(void) {
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, FlecsEngine);

    ecs_entity_t parent = ecs_new(world);
    ecs_set(world, parent, FlecsPosition3, {1, 2, 3});

    ecs_entity_t child = ecs_entity(world, { .parent = parent });
    ecs_set(world, child, FlecsPosition3, {4, 5, 6});

    const FlecsWorldTransform3 *transform = ecs_get(
        world, child, FlecsWorldTransform3);
    test_assert(transform != NULL);
    test_flt(transform->m[3][0], 5);
    test_flt(transform->m[3][1], 7);
    test_flt(transform->m[3][2], 9);

    ecs_fini(world);
}

void Transform3_gltf_preserves_root_transform(void) {
    const char *fixture = Transform3_fixture();
    test_assert(fixture != NULL);

    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, FlecsEngine);

    ecs_entity_t root = ecs_new(world);
    ecs_set(world, root, FlecsPosition3, {1, 2, 3});
    ecs_set(world, root, FlecsGltf, {fixture});

    const FlecsPosition3 *root_position = ecs_get(
        world, root, FlecsPosition3);
    test_assert(root_position != NULL);
    test_flt(root_position->x, 1);
    test_flt(root_position->y, 2);
    test_flt(root_position->z, 3);
    test_assert(!ecs_has(world, root, FlecsMesh3));

    ecs_entity_t mesh_child = Transform3_only_child(world, root);
    test_assert(mesh_child != 0);
    test_assert(ecs_has(world, mesh_child, FlecsMesh3));

    const FlecsPosition3 *mesh_position = ecs_get(
        world, mesh_child, FlecsPosition3);
    test_assert(mesh_position != NULL);
    test_flt(mesh_position->x, 4);
    test_flt(mesh_position->y, 5);
    test_flt(mesh_position->z, 6);

    const FlecsWorldTransform3 *mesh_transform = ecs_get(
        world, mesh_child, FlecsWorldTransform3);
    test_assert(mesh_transform != NULL);
    test_flt(mesh_transform->m[3][0], 5);
    test_flt(mesh_transform->m[3][1], 7);
    test_flt(mesh_transform->m[3][2], 9);

    ecs_fini(world);
}

void Transform3_gltf_non_prefab_uses_childof_storage(void) {
    const char *fixture = Transform3_fixture();
    test_assert(fixture != NULL);

    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, FlecsEngine);

    ecs_entity_t root = ecs_new(world);
    ecs_set(world, root, FlecsGltf, {fixture});

    ecs_entity_t mesh_child = Transform3_only_child(world, root);
    test_assert(mesh_child != 0);
    test_assert(ecs_table_has_id(world, ecs_get_table(world, mesh_child),
        ecs_pair(EcsChildOf, root)));
    test_assert(!ecs_owns(world, mesh_child, EcsParent));

    ecs_fini(world);
}

void Transform3_gltf_prefab_uses_parent_storage(void) {
    const char *fixture = Transform3_fixture();
    test_assert(fixture != NULL);

    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, FlecsEngine);

    ecs_entity_t prefab = ecs_new_w_id(world, EcsPrefab);
    ecs_set(world, prefab, FlecsGltf, {fixture});

    ecs_entity_t prefab_child = Transform3_only_child(world, prefab);
    test_assert(prefab_child != 0);
    test_assert(ecs_owns(world, prefab_child, EcsParent));
    test_assert(!ecs_table_has_id(world, ecs_get_table(world, prefab_child),
        ecs_pair(EcsChildOf, prefab)));

    ecs_entity_t first = ecs_new_w_pair(world, EcsIsA, prefab);
    ecs_entity_t second = ecs_new_w_pair(world, EcsIsA, prefab);
    ecs_entity_t first_child = Transform3_only_child(world, first);
    ecs_entity_t second_child = Transform3_only_child(world, second);

    test_assert(ecs_owns(world, first_child, EcsParent));
    test_assert(ecs_owns(world, second_child, EcsParent));
    test_assert(ecs_get_table(world, first_child) ==
        ecs_get_table(world, second_child));

    ecs_fini(world);
}

void Transform3_gltf_prefab_converts_childof_child(void) {
    const char *fixture = Transform3_fixture();
    test_assert(fixture != NULL);

    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, FlecsEngine);

    ecs_entity_t prefab = ecs_new_w_id(world, EcsPrefab);
    ecs_set(world, prefab, FlecsGltf, {fixture});

    ecs_entity_t child = ecs_entity(world, { .parent = prefab });
    test_assert(ecs_owns(world, child, EcsParent));
    test_assert(!ecs_table_has_id(world, ecs_get_table(world, child),
        ecs_pair(EcsChildOf, prefab)));

    ecs_entity_t instance = ecs_new_w_pair(world, EcsIsA, prefab);
    int32_t child_count = 0;
    ecs_iter_t it = ecs_children(world, instance);
    while (ecs_children_next(&it)) {
        child_count += it.count;
    }
    test_int(child_count, 2);

    ecs_fini(world);
}

void Transform3_gltf_prefab_normalizes_existing_child(void) {
    const char *fixture = Transform3_fixture();
    test_assert(fixture != NULL);

    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, FlecsEngine);

    ecs_entity_t prefab = ecs_new_w_id(world, EcsPrefab);
    ecs_entity_t child = ecs_entity(world, { .parent = prefab });
    test_assert(!ecs_owns(world, child, EcsParent));

    ecs_set(world, prefab, FlecsGltf, {fixture});
    test_assert(ecs_owns(world, child, EcsParent));
    test_assert(!ecs_table_has_id(world, ecs_get_table(world, child),
        ecs_pair(EcsChildOf, prefab)));

    ecs_entity_t instance = ecs_new_w_pair(world, EcsIsA, prefab);
    int32_t child_count = 0;
    ecs_iter_t it = ecs_children(world, instance);
    while (ecs_children_next(&it)) {
        child_count += it.count;
    }
    test_int(child_count, 2);

    ecs_fini(world);
}

void Transform3_gltf_prefab_instantiates_mesh_child(void) {
    const char *fixture = Transform3_fixture();
    test_assert(fixture != NULL);

    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, FlecsEngine);

    ecs_entity_t prefab = ecs_new_w_id(world, EcsPrefab);
    ecs_set(world, prefab, FlecsPosition3, {1, 2, 3});
    ecs_set(world, prefab, FlecsGltf, {fixture});

    const FlecsPosition3 *prefab_position = ecs_get(
        world, prefab, FlecsPosition3);
    test_assert(prefab_position != NULL);
    test_flt(prefab_position->x, 1);
    test_flt(prefab_position->y, 2);
    test_flt(prefab_position->z, 3);

    ecs_entity_t instance = ecs_new_w_pair(world, EcsIsA, prefab);
    test_assert(instance != 0);

    ecs_entity_t mesh_child = Transform3_only_child(world, instance);
    test_assert(mesh_child != 0);
    test_assert(ecs_get_parent(world, mesh_child) == instance);
    test_assert(ecs_has(world, mesh_child, FlecsMesh3));

    const FlecsPosition3 *mesh_position = ecs_get(
        world, mesh_child, FlecsPosition3);
    test_assert(mesh_position != NULL);
    test_flt(mesh_position->x, 4);
    test_flt(mesh_position->y, 5);
    test_flt(mesh_position->z, 6);

    ecs_fini(world);
}

void Transform3_gltf_nested_prefab_instantiates_mesh_child(void) {
    const char *fixture = Transform3_fixture();
    test_assert(fixture != NULL);

    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, FlecsEngine);

    /* This matches the Base prefab shape: the entity with Gltf is itself a
     * prefab child, so the generated primitive is a prefab grandchild. */
    ecs_entity_t base = ecs_new_w_id(world, EcsPrefab);
    ecs_set(world, base, FlecsScale3, {2, 2, 2});
    ecs_set(world, base, FlecsGltf, {fixture});
    ecs_entity_t landing_pad = ecs_entity(world, { .parent = base });
    ecs_set(world, landing_pad, FlecsPosition3, {0, 2, 0});
    ecs_set(world, landing_pad, FlecsGltf, {fixture});

    ecs_entity_t instance = ecs_new_w_pair(world, EcsIsA, base);
    test_assert(instance != 0);
    ecs_entity_t scene = ecs_new(world);
    ecs_add_pair(world, instance, EcsChildOf, scene);
    ecs_set(world, instance, FlecsPosition3, {10, 3, 20});

    ecs_entity_t landing_pad_instance = 0;
    int32_t child_count = 0;
    ecs_iter_t it = ecs_children(world, instance);
    while (ecs_children_next(&it)) {
        for (int32_t i = 0; i < it.count; i ++) {
            ecs_entity_t child = it.entities[i];
            const FlecsPosition3 *child_position = ecs_get(
                world, child, FlecsPosition3);
            child_count ++;
            if (child_position && child_position->y == 2) {
                landing_pad_instance = child;
            }
        }
    }
    test_int(child_count, 2);
    test_assert(landing_pad_instance != 0);
    test_assert(ecs_get_parent(world, landing_pad_instance) == instance);

    const FlecsPosition3 *position = ecs_get(
        world, landing_pad_instance, FlecsPosition3);
    test_assert(position != NULL);
    test_flt(position->x, 0);
    test_flt(position->y, 2);
    test_flt(position->z, 0);

    ecs_entity_t mesh_child = Transform3_only_child(
        world, landing_pad_instance);
    test_assert(mesh_child != 0);
    test_assert(ecs_get_parent(world, mesh_child) == landing_pad_instance);
    test_assert(ecs_has(world, mesh_child, FlecsMesh3));
    test_assert(ecs_owns(world, mesh_child, FlecsWorldTransform3));
    test_assert(ecs_owns(world, mesh_child, FlecsAABB));

    const FlecsWorldTransform3 *mesh_transform = ecs_get(
        world, mesh_child, FlecsWorldTransform3);
    test_assert(mesh_transform != NULL);
    test_flt(mesh_transform->m[3][0], 18);
    test_flt(mesh_transform->m[3][1], 17);
    test_flt(mesh_transform->m[3][2], 32);

    /* Match the essential terms used by the placed mesh render batches.
     * Prefabs are excluded by default, so both results must be instantiated
     * children of the placed Base. */
    ecs_query_t *renderable = ecs_query(world, {
        .terms = {
            { .id = ecs_id(FlecsMesh3),
              .src.id = EcsUp, .trav = EcsIsA },
            { .id = ecs_id(FlecsWorldTransform3), .src.id = EcsSelf },
            { .id = ecs_id(FlecsAABB), .src.id = EcsSelf }
        }
    });
    int32_t renderable_count = 0;
    it = ecs_query_iter(world, renderable);
    while (ecs_query_next(&it)) {
        renderable_count += it.count;
    }
    test_int(renderable_count, 2);
    ecs_query_fini(renderable);

    ecs_fini(world);
}
