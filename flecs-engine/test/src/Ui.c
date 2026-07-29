#include <flecs_engine_test.h>

#include "../../src/modules/ui/ui.h"

static const FlecsUiAlign ui_haligns[] = {
    FlecsUiLeft, FlecsUiCenter, FlecsUiRight
};

static const FlecsUiVAlign ui_valigns[] = {
    FlecsUiTop, FlecsUiMiddle, FlecsUiBottom
};

static ecs_world_t* ui_world(void) {
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, FlecsEngine);

    ecs_entity_t surface = ecs_new(world);
    ecs_set(world, surface, FlecsSurface, {
        .width = 800,
        .height = 600
    });

    FlecsEngineImpl *engine = ecs_singleton_ensure(world, FlecsEngineImpl);
    engine->surface = surface;

    return world;
}

static const FlecsUiBounds* ui_bounds(
    ecs_world_t *world,
    ecs_entity_t e)
{
    const FlecsUiBounds *bounds = ecs_get(world, e, FlecsUiBounds);
    test_assert(bounds != NULL);
    return bounds;
}

void Ui_align_offset(void) {
    test_flt(flecsEngine_uiAlignOffset(FlecsUiLeft, 100, 40), 0);
    test_flt(flecsEngine_uiAlignOffset(FlecsUiCenter, 100, 40), 30);
    test_flt(flecsEngine_uiAlignOffset(FlecsUiRight, 100, 40), 60);
    test_flt(flecsEngine_uiAlignOffset(FlecsUiCenter, 40, 100), -30);

    test_flt(flecsEngine_uiValignOffset(FlecsUiTop, 50, 20), 0);
    test_flt(flecsEngine_uiValignOffset(FlecsUiMiddle, 50, 20), 15);
    test_flt(flecsEngine_uiValignOffset(FlecsUiBottom, 50, 20), 30);
    test_flt(flecsEngine_uiValignOffset(FlecsUiMiddle, 20, 50), -15);
}

void Ui_measure_text(void) {
    ecs_world_t *world = ui_world();

    ecs_entity_t container = ecs_new(world);
    ecs_set(world, container, FlecsUiRect, {120, 32});

    ecs_entity_t label = ecs_new_w_pair(world, EcsChildOf, container);
    ecs_set(world, label, FlecsUiText, {.text = "Hello", .size = 20});

    ecs_progress(world, 0.016f);

    float tw = flecsEngine_ui2dTextWidth(world, 20, "Hello");
    test_assert(tw > 0);

    const FlecsUiBounds *b = ui_bounds(world, label);
    test_flt(b->x, 0);
    test_flt(b->y, 0);
    test_flt(b->width, tw);
    test_flt(b->height, 20);

    const FlecsUiBounds *cb = ui_bounds(world, container);
    test_flt(cb->width, 120);
    test_flt(cb->height, 32);

    ecs_fini(world);
}

void Ui_slot_align_text_matrix(void) {
    ecs_world_t *world = ui_world();

    ecs_entity_t container = ecs_new(world);
    ecs_set(world, container, FlecsUiRect, {200, 100});

    ecs_entity_t label = ecs_new_w_pair(world, EcsChildOf, container);
    ecs_set(world, label, FlecsUiText, {.text = "Hi", .size = 20});

    ecs_progress(world, 0.016f);
    float tw = flecsEngine_ui2dTextWidth(world, 20, "Hi");
    test_assert(tw > 0);

    for (int h = 0; h < 3; h ++) {
        for (int v = 0; v < 3; v ++) {
            ecs_set(world, label, FlecsUiLayout, {
                .halign = ui_haligns[h],
                .valign = ui_valigns[v]
            });

            ecs_progress(world, 0.016f);

            const FlecsUiBounds *b = ui_bounds(world, label);
            test_flt(b->x,
                flecsEngine_uiAlignOffset(ui_haligns[h], 200, tw));
            test_flt(b->y,
                flecsEngine_uiValignOffset(ui_valigns[v], 100, 20));
            test_flt(b->width, tw);
            test_flt(b->height, 20);
        }
    }

    ecs_fini(world);
}

void Ui_slot_align_rect_matrix(void) {
    ecs_world_t *world = ui_world();

    ecs_entity_t container = ecs_new(world);
    ecs_set(world, container, FlecsUiRect, {200, 100});

    ecs_entity_t child = ecs_new_w_pair(world, EcsChildOf, container);
    ecs_set(world, child, FlecsUiLayout, {.width = 50, .height = 20});

    for (int h = 0; h < 3; h ++) {
        for (int v = 0; v < 3; v ++) {
            ecs_set(world, child, FlecsUiLayout, {
                .width = 50,
                .height = 20,
                .halign = ui_haligns[h],
                .valign = ui_valigns[v]
            });

            ecs_progress(world, 0.016f);

            const FlecsUiBounds *b = ui_bounds(world, child);
            test_flt(b->x, 0);
            test_flt(b->y, 0);
            test_flt(b->width, 50);
            test_flt(b->height, 20);
        }
    }

    ecs_fini(world);
}

void Ui_container_align_child_matrix(void) {
    ecs_world_t *world = ui_world();

    ecs_entity_t container = ecs_new(world);
    ecs_entity_t child = ecs_new_w_pair(world, EcsChildOf, container);
    ecs_set(world, child, FlecsUiRect, {50, 20});

    for (int h = 0; h < 3; h ++) {
        for (int v = 0; v < 3; v ++) {
            ecs_set(world, container, FlecsUiLayout, {
                .width = 200,
                .height = 100,
                .halign = ui_haligns[h],
                .valign = ui_valigns[v]
            });

            ecs_progress(world, 0.016f);

            const FlecsUiBounds *b = ui_bounds(world, child);
            test_flt(b->x,
                flecsEngine_uiAlignOffset(ui_haligns[h], 200, 50));
            test_flt(b->y,
                flecsEngine_uiValignOffset(ui_valigns[v], 100, 20));
        }
    }

    ecs_fini(world);
}

void Ui_row_align_block_matrix(void) {
    ecs_world_t *world = ui_world();

    ecs_entity_t container = ecs_new(world);
    ecs_entity_t c1 = ecs_new_w_pair(world, EcsChildOf, container);
    ecs_set(world, c1, FlecsUiRect, {30, 20});
    ecs_entity_t c2 = ecs_new_w_pair(world, EcsChildOf, container);
    ecs_set(world, c2, FlecsUiRect, {50, 40});

    for (int h = 0; h < 3; h ++) {
        for (int v = 0; v < 3; v ++) {
            ecs_set(world, container, FlecsUiLayout, {
                .direction = FlecsUiRow,
                .spacing = 10,
                .width = 200,
                .height = 50,
                .halign = ui_haligns[h],
                .valign = ui_valigns[v]
            });

            ecs_progress(world, 0.016f);

            float dx = flecsEngine_uiAlignOffset(ui_haligns[h], 200, 90);
            float dy = flecsEngine_uiValignOffset(ui_valigns[v], 50, 40);

            const FlecsUiBounds *b1 = ui_bounds(world, c1);
            test_flt(b1->x, dx);
            test_flt(b1->y, dy);

            const FlecsUiBounds *b2 = ui_bounds(world, c2);
            test_flt(b2->x, dx + 40);
            test_flt(b2->y, dy);
        }
    }

    ecs_fini(world);
}

void Ui_column_align_block_matrix(void) {
    ecs_world_t *world = ui_world();

    ecs_entity_t container = ecs_new(world);
    ecs_entity_t c1 = ecs_new_w_pair(world, EcsChildOf, container);
    ecs_set(world, c1, FlecsUiRect, {30, 20});
    ecs_entity_t c2 = ecs_new_w_pair(world, EcsChildOf, container);
    ecs_set(world, c2, FlecsUiRect, {50, 25});

    for (int h = 0; h < 3; h ++) {
        for (int v = 0; v < 3; v ++) {
            ecs_set(world, container, FlecsUiLayout, {
                .direction = FlecsUiColumn,
                .spacing = 10,
                .width = 200,
                .height = 100,
                .halign = ui_haligns[h],
                .valign = ui_valigns[v]
            });

            ecs_progress(world, 0.016f);

            float dx = flecsEngine_uiAlignOffset(ui_haligns[h], 200, 50);
            float dy = flecsEngine_uiValignOffset(ui_valigns[v], 100, 55);

            const FlecsUiBounds *b1 = ui_bounds(world, c1);
            test_flt(b1->x, dx);
            test_flt(b1->y, dy);

            const FlecsUiBounds *b2 = ui_bounds(world, c2);
            test_flt(b2->x, dx);
            test_flt(b2->y, dy + 30);
        }
    }

    ecs_fini(world);
}

void Ui_column_mixed_slot_align(void) {
    ecs_world_t *world = ui_world();

    ecs_entity_t container = ecs_new(world);
    ecs_set(world, container, FlecsUiLayout, {
        .direction = FlecsUiColumn,
        .width = 200
    });

    ecs_entity_t c1 = ecs_new_w_pair(world, EcsChildOf, container);
    ecs_set(world, c1, FlecsUiText, {.text = "One", .size = 16});
    ecs_set(world, c1, FlecsUiLayout, {.halign = FlecsUiRight});

    ecs_entity_t c2 = ecs_new_w_pair(world, EcsChildOf, container);
    ecs_set(world, c2, FlecsUiText, {.text = "Two", .size = 16});

    ecs_progress(world, 0.016f);

    float tw1 = flecsEngine_ui2dTextWidth(world, 16, "One");
    test_assert(tw1 > 0);

    const FlecsUiBounds *b1 = ui_bounds(world, c1);
    test_flt(b1->x, 200 - tw1);
    test_flt(b1->y, 0);

    const FlecsUiBounds *b2 = ui_bounds(world, c2);
    test_flt(b2->x, 0);
    test_flt(b2->y, 16);

    ecs_fini(world);
}

void Ui_row_child_self_valign(void) {
    ecs_world_t *world = ui_world();

    ecs_entity_t container = ecs_new(world);
    ecs_set(world, container, FlecsUiLayout, {
        .direction = FlecsUiRow,
        .height = 50
    });

    ecs_entity_t c1 = ecs_new_w_pair(world, EcsChildOf, container);
    ecs_set(world, c1, FlecsUiText, {.text = "A", .size = 20});
    ecs_set(world, c1, FlecsUiLayout, {.valign = FlecsUiMiddle});

    ecs_entity_t c2 = ecs_new_w_pair(world, EcsChildOf, container);
    ecs_set(world, c2, FlecsUiText, {.text = "B", .size = 20});
    ecs_set(world, c2, FlecsUiLayout, {.valign = FlecsUiBottom});

    ecs_entity_t c3 = ecs_new_w_pair(world, EcsChildOf, container);
    ecs_set(world, c3, FlecsUiText, {.text = "C", .size = 20});

    ecs_progress(world, 0.016f);

    float tw1 = flecsEngine_ui2dTextWidth(world, 20, "A");
    float tw2 = flecsEngine_ui2dTextWidth(world, 20, "B");
    test_assert(tw1 > 0);
    test_assert(tw2 > 0);

    const FlecsUiBounds *b1 = ui_bounds(world, c1);
    test_flt(b1->x, 0);
    test_flt(b1->y, 15);

    const FlecsUiBounds *b2 = ui_bounds(world, c2);
    test_flt(b2->x, tw1);
    test_flt(b2->y, 30);

    const FlecsUiBounds *b3 = ui_bounds(world, c3);
    test_flt(b3->x, tw1 + tw2);
    test_flt(b3->y, 0);

    ecs_fini(world);
}

void Ui_container_align_padding(void) {
    ecs_world_t *world = ui_world();

    ecs_entity_t container = ecs_new(world);
    ecs_set(world, container, FlecsUiLayout, {
        .width = 200,
        .height = 100,
        .padding = 10,
        .halign = FlecsUiRight,
        .valign = FlecsUiBottom
    });

    ecs_entity_t child = ecs_new_w_pair(world, EcsChildOf, container);
    ecs_set(world, child, FlecsUiRect, {50, 20});

    ecs_progress(world, 0.016f);

    const FlecsUiBounds *b = ui_bounds(world, child);
    test_flt(b->x, 140);
    test_flt(b->y, 70);

    ecs_fini(world);
}

void Ui_text_own_padding(void) {
    ecs_world_t *world = ui_world();

    ecs_entity_t container = ecs_new(world);
    ecs_set(world, container, FlecsUiRect, {200, 100});

    ecs_entity_t label = ecs_new_w_pair(world, EcsChildOf, container);
    ecs_set(world, label, FlecsUiText, {.text = "Hi", .size = 20});
    ecs_set(world, label, FlecsUiLayout, {.padding = 10});

    ecs_progress(world, 0.016f);

    float tw = flecsEngine_ui2dTextWidth(world, 20, "Hi");
    test_assert(tw > 0);

    const FlecsUiBounds *b = ui_bounds(world, label);
    test_flt(b->x, 0);
    test_flt(b->y, 0);
    test_flt(b->width, tw + 20);
    test_flt(b->height, 40);

    ecs_fini(world);
}

void Ui_text_own_padding_slot_align(void) {
    ecs_world_t *world = ui_world();

    ecs_entity_t container = ecs_new(world);
    ecs_set(world, container, FlecsUiRect, {80, 60});

    ecs_entity_t label = ecs_new_w_pair(world, EcsChildOf, container);
    ecs_set(world, label, FlecsUiText, {.text = "1", .size = 11});
    ecs_set(world, label, FlecsUiLayout, {
        .padding = 4,
        .halign = FlecsUiRight
    });

    ecs_progress(world, 0.016f);

    float tw = flecsEngine_ui2dTextWidth(world, 11, "1");
    test_assert(tw > 0);

    const FlecsUiBounds *b = ui_bounds(world, label);
    test_flt(b->x, 80 - tw - 8);
    test_flt(b->y, 0);
    test_flt(b->width, tw + 8);
    test_flt(b->height, 19);

    ecs_fini(world);
}

void Ui_text_and_children_align(void) {
    ecs_world_t *world = ui_world();

    ecs_entity_t container = ecs_new(world);
    ecs_set(world, container, FlecsUiLayout, {
        .direction = FlecsUiColumn,
        .width = 200,
        .halign = FlecsUiCenter
    });
    ecs_set(world, container, FlecsUiText, {.text = "Title", .size = 16});

    ecs_entity_t child = ecs_new_w_pair(world, EcsChildOf, container);
    ecs_set(world, child, FlecsUiRect, {50, 20});

    ecs_progress(world, 0.016f);

    const FlecsUiBounds *cb = ui_bounds(world, container);
    test_flt(cb->width, 200);

    const FlecsUiBounds *b = ui_bounds(world, child);
    test_flt(b->x, 75);
    test_flt(b->y, 0);

    ecs_fini(world);
}

void Ui_anchor_point_matrix(void) {
    ecs_world_t *world = ui_world();

    ecs_entity_t container = ecs_new(world);
    ecs_set(world, container, FlecsUiRect, {200, 100});

    ecs_entity_t child = ecs_new_w_pair(world, EcsChildOf, container);
    ecs_set(world, child, FlecsUiRect, {40, 20});

    for (int ah = 0; ah < 3; ah ++) {
        for (int av = 0; av < 3; av ++) {
            for (int h = 0; h < 3; h ++) {
                for (int v = 0; v < 3; v ++) {
                    ecs_set(world, child, FlecsUiAnchor, {
                        .h = ui_haligns[ah],
                        .v = ui_valigns[av]
                    });
                    ecs_set(world, child, FlecsUiLayout, {
                        .halign = ui_haligns[h],
                        .valign = ui_valigns[v]
                    });

                    ecs_progress(world, 0.016f);

                    const FlecsUiBounds *b = ui_bounds(world, child);
                    test_flt(b->x,
                        flecsEngine_uiAlignOffset(ui_haligns[ah], 200, 0) +
                        flecsEngine_uiAlignOffset(ui_haligns[h], 0, 40));
                    test_flt(b->y,
                        flecsEngine_uiValignOffset(ui_valigns[av], 100, 0) +
                        flecsEngine_uiValignOffset(ui_valigns[v], 0, 20));
                }
            }
        }
    }

    ecs_fini(world);
}

void Ui_anchor_screen(void) {
    ecs_world_t *world = ui_world();

    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, FlecsUiRect, {50, 20});
    ecs_set(world, e, FlecsUiAnchor, {
        .h = FlecsUiRight,
        .v = FlecsUiBottom,
        .x = -10,
        .y = -5
    });
    ecs_set(world, e, FlecsUiLayout, {
        .halign = FlecsUiRight,
        .valign = FlecsUiBottom
    });

    ecs_progress(world, 0.016f);

    const FlecsUiBounds *b = ui_bounds(world, e);
    test_flt(b->x, 740);
    test_flt(b->y, 575);

    ecs_fini(world);
}

void Ui_anchor_out_of_flow(void) {
    ecs_world_t *world = ui_world();

    ecs_entity_t container = ecs_new(world);
    ecs_set(world, container, FlecsUiLayout, {
        .direction = FlecsUiColumn,
        .spacing = 5
    });

    ecs_entity_t c1 = ecs_new_w_pair(world, EcsChildOf, container);
    ecs_set(world, c1, FlecsUiRect, {30, 10});

    ecs_entity_t anchored = ecs_new_w_pair(world, EcsChildOf, container);
    ecs_set(world, anchored, FlecsUiRect, {40, 20});
    ecs_set(world, anchored, FlecsUiAnchor, {.v = FlecsUiTop});
    ecs_set(world, anchored, FlecsUiLayout, {.valign = FlecsUiBottom});

    ecs_entity_t c2 = ecs_new_w_pair(world, EcsChildOf, container);
    ecs_set(world, c2, FlecsUiRect, {30, 15});

    ecs_progress(world, 0.016f);

    const FlecsUiBounds *cb = ui_bounds(world, container);
    test_flt(cb->width, 30);
    test_flt(cb->height, 30);

    const FlecsUiBounds *b1 = ui_bounds(world, c1);
    test_flt(b1->y, 0);

    const FlecsUiBounds *b2 = ui_bounds(world, c2);
    test_flt(b2->y, 15);

    const FlecsUiBounds *ba = ui_bounds(world, anchored);
    test_flt(ba->x, 0);
    test_flt(ba->y, -20);

    ecs_fini(world);
}

void Ui_anchor_above_parent(void) {
    ecs_world_t *world = ui_world();

    ecs_entity_t container = ecs_new(world);
    ecs_set(world, container, FlecsUiRect, {80, 60});

    ecs_entity_t tooltip = ecs_new_w_pair(world, EcsChildOf, container);
    ecs_set(world, tooltip, FlecsUiRect, {0, 0});
    ecs_set(world, tooltip, FlecsUiAnchor, {.v = FlecsUiTop, .y = -4});
    ecs_set(world, tooltip, FlecsUiLayout, {
        .direction = FlecsUiColumn,
        .valign = FlecsUiBottom
    });

    ecs_entity_t content = ecs_new_w_pair(world, EcsChildOf, tooltip);
    ecs_set(world, content, FlecsUiRect, {30, 10});

    ecs_progress(world, 0.016f);

    const FlecsUiBounds *b = ui_bounds(world, tooltip);
    test_flt(b->x, 0);
    test_flt(b->y, -14);
    test_flt(b->width, 30);
    test_flt(b->height, 10);

    const FlecsUiBounds *cb = ui_bounds(world, content);
    test_flt(cb->x, 0);
    test_flt(cb->y, -14);

    ecs_fini(world);
}

void Ui_defaults_unchanged(void) {
    ecs_world_t *world = ui_world();

    ecs_entity_t container = ecs_new(world);
    ecs_set(world, container, FlecsUiRect, {120, 32});

    ecs_entity_t c1 = ecs_new_w_pair(world, EcsChildOf, container);
    ecs_set(world, c1, FlecsUiText, {.text = "Hello", .size = 16});
    ecs_set(world, c1, FlecsUiLayout, {0});

    ecs_entity_t c2 = ecs_new_w_pair(world, EcsChildOf, container);
    ecs_set(world, c2, FlecsUiText, {.text = "World", .size = 16});

    ecs_progress(world, 0.016f);

    const FlecsUiBounds *b1 = ui_bounds(world, c1);
    test_flt(b1->x, 0);
    test_flt(b1->y, 0);

    const FlecsUiBounds *b2 = ui_bounds(world, c2);
    test_flt(b2->x, 0);
    test_flt(b2->y, 0);

    ecs_fini(world);
}
