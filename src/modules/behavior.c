#define BIOME_BEHAVIOR_IMPL

#include "biome.h"

ECS_COMPONENT_DECLARE(BiomeBehaviors);

typedef struct BiomeBehaviorRuntime {
    ecs_script_task_t **tasks;
    int32_t count;
} BiomeBehaviorRuntime;

ECS_COMPONENT_DECLARE(BiomeBehaviorRuntime);

static void BiomeBehaviorRuntime_fini(
    BiomeBehaviorRuntime *runtime)
{
    for (int32_t i = 0; i < runtime->count; i ++) {
        if (runtime->tasks[i]) {
            ecs_script_task_free(runtime->tasks[i]);
        }
    }
    ecs_os_free(runtime->tasks);
    ecs_os_zeromem(runtime);
}

ECS_CTOR(BiomeBehaviorRuntime, ptr, {
    ecs_os_zeromem(ptr);
})

ECS_MOVE(BiomeBehaviorRuntime, dst, src, {
    BiomeBehaviorRuntime_fini(dst);
    *dst = *src;
    ecs_os_zeromem(src);
})

ECS_DTOR(BiomeBehaviorRuntime, ptr, {
    BiomeBehaviorRuntime_fini(ptr);
})

static bool biome_behavior_resume(
    ecs_world_t *world,
    ecs_entity_t entity,
    ecs_script_task_t *task)
{
    ecs_script_eval_result_t result = {0};
    ecs_script_task_status_t status = ecs_script_task_resume(task, &result);
    if (status == EcsScriptTaskError) {
        char *path = ecs_get_path(world, entity);
        ecs_err("behavior for '%s' failed: %s",
            path ? path : "<anonymous>",
            result.error ? result.error : "unknown script error");
        ecs_os_free(path);
    }
    ecs_os_free(result.error);
    return status == EcsScriptTaskPending;
}

static void BiomeBehaviorStart(ecs_iter_t *it) {
    const BiomeBehaviors *behaviors = ecs_field(it, BiomeBehaviors, 0);
    bool self = ecs_field_is_self(it, 0);

    for (int32_t i = 0; i < it->count; i ++) {
        const BiomeBehaviors *value = self ? &behaviors[i] : behaviors;
        int32_t count = ecs_vec_count(value);
        BiomeBehaviorRuntime runtime = {
            .tasks = ecs_os_calloc_n(ecs_script_task_t*, count),
            .count = count
        };
        ecs_entity_t *scripts = ecs_vec_first_t(value, ecs_entity_t);

        for (int32_t t = 0; t < count; t ++) {
            const EcsScript *script = ecs_get(it->world, scripts[t], EcsScript);
            if (!script || !script->script) {
                continue;
            }
            runtime.tasks[t] = ecs_script_task_new(script->script,
                &(ecs_script_task_desc_t){
                    .entity = it->entities[i],
                    .loop = EcsScriptTaskLoopForever
                });
            ecs_script_task_t *task = runtime.tasks[t];
            if (task && !biome_behavior_resume(
                it->world, it->entities[i], task))
            {
                ecs_script_task_free(task);
                runtime.tasks[t] = NULL;
            }
        }

        ecs_set_ptr(it->world, it->entities[i],
            BiomeBehaviorRuntime, &runtime);
    }
}

static void BiomeBehaviorUpdate(ecs_iter_t *it) {
    BiomeBehaviorRuntime *runtimes = ecs_field(
        it, BiomeBehaviorRuntime, 0);

    for (int32_t i = 0; i < it->count; i ++) {
        for (int32_t t = 0; t < runtimes[i].count; t ++) {
            ecs_script_task_t *task = runtimes[i].tasks[t];
            if (task && ecs_script_task_is_ready(task)) {
                if (!biome_behavior_resume(
                    it->world, it->entities[i], task))
                {
                    ecs_script_task_free(task);
                    runtimes[i].tasks[t] = NULL;
                }
            }
        }
    }
}

void biomeBehaviorImport(ecs_world_t *world) {
    ECS_MODULE(world, biomeBehavior);

    ecs_id(BiomeBehaviors) = ecs_vector(world, {
        .entity = ecs_entity(world, {
            .name = "Behaviors",
            .symbol = "BiomeBehaviors"
        }),
        .type = ecs_id(ecs_entity_t)
    });

    ecs_set_name_prefix(world, "BiomeBehavior");
    ECS_COMPONENT_DEFINE(world, BiomeBehaviorRuntime);
    ecs_set_hooks(world, BiomeBehaviorRuntime, {
        .ctor = ecs_ctor(BiomeBehaviorRuntime),
        .move = ecs_move(BiomeBehaviorRuntime),
        .dtor = ecs_dtor(BiomeBehaviorRuntime),
        .flags = ECS_TYPE_HOOK_COPY_ILLEGAL
    });

    ecs_system(world, {
        .entity = ecs_entity(world, { .name = "Start" }),
        .query.terms = {
            { .id = ecs_id(BiomeBehaviors), .inout = EcsIn },
            { .id = ecs_id(BiomeBehaviorRuntime), .oper = EcsNot }
        },
        .phase = EcsPreUpdate,
        .callback = BiomeBehaviorStart
    });

    ecs_system(world, {
        .entity = ecs_entity(world, { .name = "Update" }),
        .query.terms = {
            { .id = ecs_id(BiomeBehaviorRuntime) }
        },
        .phase = EcsPreUpdate,
        .callback = BiomeBehaviorUpdate
    });
}
