#include "material.h"

ECS_COMPONENT_DECLARE(FlecsRgba);
ECS_COMPONENT_DECLARE(FlecsTint);
ECS_COMPONENT_DECLARE(FlecsActualTint);
ECS_COMPONENT_DECLARE(FlecsPbrMaterial);
ECS_COMPONENT_DECLARE(FlecsEmissive);
ECS_COMPONENT_DECLARE(FlecsMaterialId);
ECS_COMPONENT_DECLARE(FlecsPbrTextures);
ECS_TAG_DECLARE(FlecsAlphaBlend);
ECS_TAG_DECLARE(FlecsVertexColors);
ECS_COMPONENT_DECLARE(FlecsTransmission);
ECS_COMPONENT_DECLARE(FlecsTextureTransform);

static void FlecsMaterialIdInit(
    ecs_iter_t *it)
{
    ecs_world_t *world = it->world;
    FlecsEngineImpl *impl = ecs_singleton_ensure(world, FlecsEngineImpl);
    FlecsMaterialId *material_id = ecs_field(it, FlecsMaterialId, 0);

    for (int32_t i = 0; i < it->count; i ++) {
        ecs_entity_t e = it->entities[i];
        (void)e;

        material_id[i].value = impl->materials.next_id;
        impl->materials.next_id ++;
    }

    /* New materials need to be uploaded on the next frame. */
    impl->materials.dirty_version ++;
}

static void FlecsMaterialDirty(
    ecs_iter_t *it)
{
    FlecsEngineImpl *impl = ecs_singleton_ensure(it->world, FlecsEngineImpl);
    impl->materials.dirty_version ++;
}

static void flecsEngine_materialAssignActualTint(
    ecs_world_t *world,
    ecs_entity_t entity,
    const FlecsTint *t,
    const FlecsTransitionValue *transition,
    const FlecsActualTint *actual)
{
    float r = transition ? transition->value[0] : t->r;
    float g = transition ? transition->value[1] : t->g;
    float b = transition ? transition->value[2] : t->b;
    float a = transition ? transition->value[3] : t->a;
    FlecsActualTint value = {
        .r = (uint8_t)glm_clamp(r + .5f, 0, 255),
        .g = (uint8_t)glm_clamp(g + .5f, 0, 255),
        .b = (uint8_t)glm_clamp(b + .5f, 0, 255),
        .a = (uint8_t)glm_clamp(a + .5f, 0, 255)
    };
    if (actual && actual->r == value.r && actual->g == value.g &&
        actual->b == value.b && actual->a == value.a)
    {
        return;
    }
    ecs_set_ptr(world, entity, FlecsActualTint, &value);
}

static void FlecsAssignActualTint(
    ecs_iter_t *it)
{
    const FlecsTint *tints = ecs_field(it, FlecsTint, 0);
    const FlecsActualTint *actual = ecs_field(it, FlecsActualTint, 1);
    ecs_entity_t tint_src = ecs_field_src(it, 0);
    const FlecsTransitionValue *shared_transition = NULL;

    if (tint_src) {
        shared_transition = flecs_transition_value_get(
            it->world, tint_src, ecs_id(FlecsTint));
    }

    for (int32_t i = 0; i < it->count; i ++) {
        const FlecsTint *t = tint_src ? tints : &tints[i];
        const FlecsTransitionValue *transition = tint_src
            ? shared_transition
            : flecs_transition_value_get(
                it->world, it->entities[i], ecs_id(FlecsTint));
        flecsEngine_materialAssignActualTint(
            it->world, it->entities[i], t, transition,
            actual ? &actual[i] : NULL);
    }
}

static void FlecsClearActualTint(
    ecs_iter_t *it)
{
    for (int32_t i = 0; i < it->count; i ++) {
        ecs_remove(it->world, it->entities[i], FlecsActualTint);
    }
}

void FlecsEngineMaterialImport(
    ecs_world_t *world)
{
    ECS_MODULE(world, FlecsEngineMaterial);

    ecs_set_name_prefix(world, "Flecs");

    ECS_COMPONENT_DEFINE(world, FlecsMaterialId);
    ECS_COMPONENT_DEFINE(world, FlecsPbrMaterial);
    ECS_COMPONENT_DEFINE(world, FlecsPbrTextures);
    ECS_COMPONENT_DEFINE(world, FlecsRgba);
    ECS_COMPONENT_DEFINE(world, FlecsTint);
    ECS_COMPONENT_DEFINE(world, FlecsActualTint);
    ECS_COMPONENT_DEFINE(world, FlecsEmissive);
    ECS_TAG_DEFINE(world, FlecsAlphaBlend);
    ECS_TAG_DEFINE(world, FlecsVertexColors);
    ECS_COMPONENT_DEFINE(world, FlecsTransmission);
    ECS_COMPONENT_DEFINE(world, FlecsTextureTransform);

    ecs_struct(world, {
        .entity = ecs_id(FlecsMaterialId),
        .members = {
            { .name = "value", .type = ecs_id(ecs_u32_t) }
        }
    });

    ecs_struct(world, {
        .entity = ecs_id(FlecsPbrMaterial),
        .members = {
            { .name = "metallic", .type = ecs_id(ecs_f32_t) },
            { .name = "roughness", .type = ecs_id(ecs_f32_t) }
        }
    });

    ecs_struct(world, {
        .entity = ecs_id(FlecsPbrTextures),
        .members = {
            { .name = "albedo", .type = ecs_id(ecs_entity_t) },
            { .name = "emissive", .type = ecs_id(ecs_entity_t) },
            { .name = "roughness", .type = ecs_id(ecs_entity_t) },
            { .name = "normal", .type = ecs_id(ecs_entity_t) }
        }
    });

    ecs_struct(world, {
        .entity = ecs_id(FlecsRgba),
        .members = {
            { .name = "r", .type = ecs_id(ecs_u8_t) },
            { .name = "g", .type = ecs_id(ecs_u8_t) },
            { .name = "b", .type = ecs_id(ecs_u8_t) },
            { .name = "a", .type = ecs_id(ecs_u8_t) },
        }
    });

    ecs_struct(world, {
        .entity = ecs_id(FlecsTint),
        .members = {
            { .name = "r", .type = ecs_id(ecs_u8_t) },
            { .name = "g", .type = ecs_id(ecs_u8_t) },
            { .name = "b", .type = ecs_id(ecs_u8_t) },
            { .name = "a", .type = ecs_id(ecs_u8_t) },
        }
    });

    ecs_struct(world, {
        .entity = ecs_id(FlecsActualTint),
        .members = {
            { .name = "r", .type = ecs_id(ecs_u8_t) },
            { .name = "g", .type = ecs_id(ecs_u8_t) },
            { .name = "b", .type = ecs_id(ecs_u8_t) },
            { .name = "a", .type = ecs_id(ecs_u8_t) },
        }
    });

    ecs_struct(world, {
        .entity = ecs_id(FlecsEmissive),
        .members = {
            { .name = "strength", .type = ecs_id(ecs_f32_t) },
            { .name = "color", .type = ecs_id(FlecsRgba) }
        }
    });

    ecs_struct(world, {
        .entity = ecs_id(FlecsTransmission),
        .members = {
            { .name = "transmission_factor", .type = ecs_id(ecs_f32_t) },
            { .name = "ior", .type = ecs_id(ecs_f32_t) },
            { .name = "thickness_factor", .type = ecs_id(ecs_f32_t) },
            { .name = "attenuation_distance", .type = ecs_id(ecs_f32_t) },
            { .name = "attenuation_color", .type = ecs_id(FlecsRgba) }
        }
    });

    ecs_struct(world, {
        .entity = ecs_id(FlecsTextureTransform),
        .members = {
            { .name = "scale_x", .type = ecs_id(ecs_f32_t) },
            { .name = "scale_y", .type = ecs_id(ecs_f32_t) },
            { .name = "offset_x", .type = ecs_id(ecs_f32_t) },
            { .name = "offset_y", .type = ecs_id(ecs_f32_t) }
        }
    });

    ecs_add_pair(world, ecs_id(FlecsMaterialId), EcsOnInstantiate, EcsInherit);
    ecs_add_pair(world, ecs_id(FlecsPbrMaterial), EcsOnInstantiate, EcsInherit);
    ecs_add_pair(world, ecs_id(FlecsPbrTextures), EcsOnInstantiate, EcsInherit);
    ecs_add_pair(world, ecs_id(FlecsRgba), EcsOnInstantiate, EcsInherit);
    ecs_add_pair(world, ecs_id(FlecsTint), EcsOnInstantiate, EcsInherit);
    ecs_add_pair(world, ecs_id(FlecsEmissive), EcsOnInstantiate, EcsInherit);
    ecs_add_pair(world, ecs_id(FlecsTransmission), EcsOnInstantiate, EcsInherit);
    ecs_add_pair(world, ecs_id(FlecsTextureTransform), EcsOnInstantiate, EcsInherit);
    ecs_add_pair(world, FlecsAlphaBlend, EcsOnInstantiate, EcsInherit);
    ecs_add_pair(world, FlecsVertexColors, EcsOnInstantiate, EcsInherit);

    ecs_add_pair(world, ecs_id(FlecsRgba), EcsWith, ecs_id(FlecsMaterialId));
    ecs_add_pair(world, ecs_id(FlecsPbrMaterial), EcsWith, ecs_id(FlecsMaterialId));
    ecs_add_pair(world, ecs_id(FlecsPbrTextures), EcsWith, ecs_id(FlecsMaterialId));

    ecs_observer(world, {
        .entity = ecs_entity(world, {
            .parent = ecs_lookup(world, "flecs.engine.material"),
            .name = "InitMaterial"
        }),
        .query.terms = {
            { .id = ecs_id(FlecsMaterialId), .src.id = EcsSelf },
            { .id = EcsPrefab, .src.id = EcsSelf }
        },
        .events = {EcsOnAdd},
        .yield_existing = true,
        .callback = FlecsMaterialIdInit
    });

    ecs_observer(world, {
        .query.terms = {
            { .id = ecs_id(FlecsRgba), .src.id = EcsSelf },
            { .id = EcsPrefab, .src.id = EcsSelf }
        },
        .events = {EcsOnSet, EcsOnRemove},
        .callback = FlecsMaterialDirty
    });
    ecs_observer(world, {
        .query.terms = {
            { .id = ecs_id(FlecsPbrMaterial), .src.id = EcsSelf },
            { .id = EcsPrefab, .src.id = EcsSelf }
        },
        .events = {EcsOnSet, EcsOnRemove},
        .callback = FlecsMaterialDirty
    });
    ecs_observer(world, {
        .query.terms = {
            { .id = ecs_id(FlecsEmissive), .src.id = EcsSelf },
            { .id = EcsPrefab, .src.id = EcsSelf }
        },
        .events = {EcsOnSet, EcsOnRemove},
        .callback = FlecsMaterialDirty
    });
    ecs_observer(world, {
        .query.terms = {
            { .id = ecs_id(FlecsTransmission), .src.id = EcsSelf },
            { .id = EcsPrefab, .src.id = EcsSelf }
        },
        .events = {EcsOnSet, EcsOnRemove},
        .callback = FlecsMaterialDirty
    });
    ecs_observer(world, {
        .query.terms = {
            { .id = ecs_id(FlecsTextureTransform), .src.id = EcsSelf },
            { .id = EcsPrefab, .src.id = EcsSelf }
        },
        .events = {EcsOnSet, EcsOnRemove},
        .callback = FlecsMaterialDirty
    });
    ecs_observer(world, {
        .query.terms = {
            { .id = ecs_pair(ecs_id(FlecsTransitionValue), EcsWildcard),
                .src.id = EcsSelf },
            { .id = EcsPrefab, .src.id = EcsSelf }
        },
        .events = {EcsOnSet, EcsOnRemove},
        .callback = FlecsMaterialDirty
    });

    ecs_system(world, {
        .entity = ecs_entity(world, {
            .name = "AssignActualTint",
            .add = ecs_ids(ecs_dependson(EcsPreStore))
        }),
        .query.terms = {
            { .id = ecs_id(FlecsTint), .src.id = EcsSelf|EcsUp,
                .trav = EcsChildOf, .inout = EcsIn },
            { .id = ecs_id(FlecsActualTint), .src.id = EcsSelf,
                .oper = EcsOptional, .inout = EcsIn },
            { .id = ecs_id(FlecsActualTint), .src.id = EcsIsEntity,
                .inout = EcsOut }
        },
        .callback = FlecsAssignActualTint
    });

    ecs_system(world, {
        .entity = ecs_entity(world, {
            .name = "ClearActualTint",
            .add = ecs_ids(ecs_dependson(EcsPreStore))
        }),
        .query.terms = {
            { .id = ecs_id(FlecsActualTint), .src.id = EcsSelf,
                .inout = EcsIn },
            { .id = ecs_id(FlecsTint), .src.id = EcsSelf|EcsUp,
                .trav = EcsChildOf, .oper = EcsNot },
            { .id = ecs_id(FlecsActualTint), .src.id = EcsIsEntity,
                .inout = EcsOut }
        },
        .callback = FlecsClearActualTint
    });
}
