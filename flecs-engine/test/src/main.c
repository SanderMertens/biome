
/* A friendly warning from bake.test
 * ----------------------------------------------------------------------------
 * This file is generated. To add/remove testcases modify the 'project.json' of
 * the test project. ANY CHANGE TO THIS FILE IS LOST AFTER (RE)BUILDING!
 * ----------------------------------------------------------------------------
 */

#include <flecs_engine_test.h>

// Testsuite 'Transform3'
void Transform3_child_world_transform(void);
void Transform3_gltf_preserves_root_transform(void);
void Transform3_gltf_prefab_instantiates_mesh_child(void);
void Transform3_gltf_nested_prefab_instantiates_mesh_child(void);

// Testsuite 'Terrain'
void Terrain_set_height_flattens_footprint(void);
void Terrain_set_height_refreshes_positions(void);

// Testsuite 'Transition'
void Transition_matrix(void);
void Transition_f64(void);
void Transition_reject_mixed(void);
void Transition_integer(void);
void Transition_reject_too_many(void);
void Transition_immediate_missing_tint(void);
void Transition_static_transform(void);
void Transition_tint(void);
void Transition_childof_tint(void);
void Transition_parent_tint(void);

bake_test_case Transform3_testcases[] = {
    {
        "child_world_transform",
        Transform3_child_world_transform
    },
    {
        "gltf_preserves_root_transform",
        Transform3_gltf_preserves_root_transform
    },
    {
        "gltf_prefab_instantiates_mesh_child",
        Transform3_gltf_prefab_instantiates_mesh_child
    },
    {
        "gltf_nested_prefab_instantiates_mesh_child",
        Transform3_gltf_nested_prefab_instantiates_mesh_child
    }
};

bake_test_case Terrain_testcases[] = {
    {
        "set_height_flattens_footprint",
        Terrain_set_height_flattens_footprint
    },
    {
        "set_height_refreshes_positions",
        Terrain_set_height_refreshes_positions
    }
};

bake_test_case Transition_testcases[] = {
    {
        "matrix",
        Transition_matrix
    },
    {
        "f64",
        Transition_f64
    },
    {
        "reject_mixed",
        Transition_reject_mixed
    },
    {
        "integer",
        Transition_integer
    },
    {
        "reject_too_many",
        Transition_reject_too_many
    },
    {
        "immediate_missing_tint",
        Transition_immediate_missing_tint
    },
    {
        "static_transform",
        Transition_static_transform
    },
    {
        "tint",
        Transition_tint
    },
    {
        "childof_tint",
        Transition_childof_tint
    },
    {
        "parent_tint",
        Transition_parent_tint
    }
};

static bake_test_suite suites[] = {
    {
        "Transform3",
        NULL,
        NULL,
        4,
        Transform3_testcases
    },
    {
        "Terrain",
        NULL,
        NULL,
        2,
        Terrain_testcases
    },
    {
        "Transition",
        NULL,
        NULL,
        10,
        Transition_testcases
    }
};

int main(int argc, char *argv[]) {
    return bake_test_run("flecs_engine_test", argc, argv, suites, 3);
}
