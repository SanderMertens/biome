
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
void Transform3_gltf_non_prefab_uses_childof_storage(void);
void Transform3_gltf_prefab_uses_parent_storage(void);
void Transform3_gltf_prefab_converts_childof_child(void);
void Transform3_gltf_prefab_normalizes_existing_child(void);
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
void Transition_distinct_parent_tints(void);
void Transition_tint_hierarchy_matrix(void);

// Testsuite 'Renderer'
void Renderer_batch_permutations(void);

// Testsuite 'Ui'
void Ui_align_offset(void);
void Ui_measure_text(void);
void Ui_slot_align_text_matrix(void);
void Ui_slot_align_rect_matrix(void);
void Ui_container_align_child_matrix(void);
void Ui_row_align_block_matrix(void);
void Ui_column_align_block_matrix(void);
void Ui_column_mixed_slot_align(void);
void Ui_row_child_self_valign(void);
void Ui_container_align_padding(void);
void Ui_text_own_padding(void);
void Ui_text_own_padding_slot_align(void);
void Ui_text_and_children_align(void);
void Ui_anchor_point_matrix(void);
void Ui_anchor_screen(void);
void Ui_anchor_out_of_flow(void);
void Ui_anchor_above_parent(void);
void Ui_defaults_unchanged(void);

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
        "gltf_non_prefab_uses_childof_storage",
        Transform3_gltf_non_prefab_uses_childof_storage
    },
    {
        "gltf_prefab_uses_parent_storage",
        Transform3_gltf_prefab_uses_parent_storage
    },
    {
        "gltf_prefab_converts_childof_child",
        Transform3_gltf_prefab_converts_childof_child
    },
    {
        "gltf_prefab_normalizes_existing_child",
        Transform3_gltf_prefab_normalizes_existing_child
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
    },
    {
        "distinct_parent_tints",
        Transition_distinct_parent_tints
    },
    {
        "tint_hierarchy_matrix",
        Transition_tint_hierarchy_matrix
    }
};

bake_test_case Renderer_testcases[] = {
    {
        "batch_permutations",
        Renderer_batch_permutations
    }
};

bake_test_case Ui_testcases[] = {
    {
        "align_offset",
        Ui_align_offset
    },
    {
        "measure_text",
        Ui_measure_text
    },
    {
        "slot_align_text_matrix",
        Ui_slot_align_text_matrix
    },
    {
        "slot_align_rect_matrix",
        Ui_slot_align_rect_matrix
    },
    {
        "container_align_child_matrix",
        Ui_container_align_child_matrix
    },
    {
        "row_align_block_matrix",
        Ui_row_align_block_matrix
    },
    {
        "column_align_block_matrix",
        Ui_column_align_block_matrix
    },
    {
        "column_mixed_slot_align",
        Ui_column_mixed_slot_align
    },
    {
        "row_child_self_valign",
        Ui_row_child_self_valign
    },
    {
        "container_align_padding",
        Ui_container_align_padding
    },
    {
        "text_own_padding",
        Ui_text_own_padding
    },
    {
        "text_own_padding_slot_align",
        Ui_text_own_padding_slot_align
    },
    {
        "text_and_children_align",
        Ui_text_and_children_align
    },
    {
        "anchor_point_matrix",
        Ui_anchor_point_matrix
    },
    {
        "anchor_screen",
        Ui_anchor_screen
    },
    {
        "anchor_out_of_flow",
        Ui_anchor_out_of_flow
    },
    {
        "anchor_above_parent",
        Ui_anchor_above_parent
    },
    {
        "defaults_unchanged",
        Ui_defaults_unchanged
    }
};

static bake_test_suite suites[] = {
    {
        "Transform3",
        NULL,
        NULL,
        8,
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
        12,
        Transition_testcases
    },
    {
        "Renderer",
        NULL,
        NULL,
        1,
        Renderer_testcases
    },
    {
        "Ui",
        NULL,
        NULL,
        18,
        Ui_testcases
    }
};

int main(int argc, char *argv[]) {
    return bake_test_run("flecs_engine_test", argc, argv, suites, 5);
}
