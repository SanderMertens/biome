#include <biome.h>
#include <biome_test.h>
#include <stdio.h>

/* The suite can be started from the project root or from test/, so resolve
 * asset paths against both. */
static const char* asset_path(const char *relative) {
    static char path[512];
    const char *prefixes[] = { "", "../" };
    for (int32_t i = 0; i < 2; i ++) {
        snprintf(path, sizeof(path), "%s%s", prefixes[i], relative);
        FILE *f = fopen(path, "rb");
        if (f) {
            fclose(f);
            return path;
        }
    }
    return NULL;
}

/* Loading an asset creates materials, which brings up FlecsEngineImpl. The
 * renderer has to be initialized for that singleton to be complete, so give
 * the world a throwaway offscreen surface and pump a frame. */
static void init_engine(ecs_world_t *world) {
    const char *output = "/tmp/biome_gltf_surface.ppm";
    remove(output);

    ecs_entity_t surface = ecs_entity(world, { .name = "surface" });
    ecs_set(world, surface, FlecsSurface, {
        .width = 8,
        .height = 8,
        .resolution_scale = 1,
        .msaa = FlecsMsaaOff,
        .write_to_file = output
    });

    for (int32_t i = 0; i < 2; i ++) {
        if (!ecs_progress(world, 0.016f)) {
            break;
        }
    }
}

static int32_t count_meshes(ecs_world_t *world, ecs_entity_t e) {
    int32_t result = ecs_get(world, e, FlecsMesh3) != NULL;

    ecs_iter_t it = ecs_children(world, e);
    while (ecs_children_next(&it)) {
        for (int32_t i = 0; i < it.count; i ++) {
            result += count_meshes(world, it.entities[i]);
        }
    }

    return result;
}

/* Buildings author their placement on the same entity that owns Gltf (see
 * cfg/buildings.flecs). Loading the asset must not overwrite that transform,
 * or every building with an offset or scale silently moves. */
void Gltf_keep_authored_transform(void) {
    const char *asset = asset_path(
        "etc/assets/kaykit/space/Assets/gltf/basemodule_F.gltf");
    test_assert(asset != NULL);

    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, FlecsEngine);
    init_engine(world);

    ecs_entity_t prefab = ecs_entity(world, {
        .name = "Building",
        .add = ecs_ids(EcsPrefab)
    });
    ecs_set(world, prefab, FlecsPosition3, {0, 0.75f, 0});
    ecs_set(world, prefab, FlecsScale3, {2, 2, 2});
    ecs_set(world, prefab, FlecsGltf, { .file = asset });

    const FlecsPosition3 *p = ecs_get(world, prefab, FlecsPosition3);
    test_assert(p != NULL);
    test_flt(p->x, 0);
    test_flt(p->y, 0.75f);
    test_flt(p->z, 0);

    const FlecsScale3 *s = ecs_get(world, prefab, FlecsScale3);
    test_assert(s != NULL);
    test_flt(s->x, 2);
    test_flt(s->y, 2);
    test_flt(s->z, 2);

    /* Instances that don't override the transform inherit the authored one */
    ecs_entity_t instance = ecs_new_w_pair(world, EcsIsA, prefab);
    const FlecsPosition3 *ip = ecs_get(world, instance, FlecsPosition3);
    test_assert(ip != NULL);
    test_flt(ip->y, 0.75f);

    test_assert(count_meshes(world, prefab) > 0);

    ecs_fini(world);
}

/* Scattered props are instances of prefabs that own Gltf. The asset meshes
 * are added as prefab children, which must survive instantiation. */
void Gltf_instantiate_asset_meshes(void) {
    const char *asset = asset_path(
        "etc/assets/kenney/nature-kit/Models/GLTF format/stone_largeA.glb");
    test_assert(asset != NULL);

    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, FlecsEngine);
    init_engine(world);

    ecs_entity_t base = ecs_entity(world, {
        .name = "Deposit",
        .add = ecs_ids(EcsPrefab)
    });
    ecs_set(world, base, FlecsScale3, {2, 2, 2});

    ecs_entity_t prefab = ecs_entity(world, {
        .name = "DepositA",
        .add = ecs_ids(EcsPrefab)
    });
    ecs_add_pair(world, prefab, EcsIsA, base);
    ecs_set(world, prefab, FlecsGltf, { .file = asset });

    int32_t prefab_meshes = count_meshes(world, prefab);
    test_assert(prefab_meshes > 0);

    /* The scale inherited from the base prefab is not replaced by the scale
     * stored in the asset. */
    const FlecsScale3 *s = ecs_get(world, prefab, FlecsScale3);
    test_assert(s != NULL);
    test_flt(s->x, 2);
    test_flt(s->y, 2);
    test_flt(s->z, 2);

    ecs_entity_t instance = ecs_new_w_pair(world, EcsIsA, prefab);
    ecs_set(world, instance, FlecsPosition3, {10, 0, 10});

    test_int(count_meshes(world, instance), prefab_meshes);

    const FlecsScale3 *is = ecs_get(world, instance, FlecsScale3);
    test_assert(is != NULL);
    test_flt(is->x, 2);
    test_flt(is->y, 2);
    test_flt(is->z, 2);

    ecs_fini(world);
}
