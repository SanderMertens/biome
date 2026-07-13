#include "biome.h"

static void biome_ui_toolButtonUp(
    ecs_world_t *world,
    ecs_entity_t widget,
    void *ctx)
{
    (void)widget;
    biomeToolPlaceBuilding(world, (ecs_entity_t)(uintptr_t)ctx);
}

static void biome_ui_dozeButtonUp(
    ecs_world_t *world,
    ecs_entity_t widget,
    void *ctx)
{
    (void)widget;
    (void)ctx;
    biomeToolDoze(world);
}

static void biome_ui_bindDozeButton(
    ecs_world_t *world,
    const char *button_path)
{
    ecs_entity_t button = ecs_lookup(world, button_path);
    if (!button || ecs_has(world, button, FlecsUiEventListener)) {
        return;
    }

    ecs_set(world, button, FlecsUiEventListener, {
        .on_lmb_up = biome_ui_dozeButtonUp
    });
}

static void biome_ui_bindToolButton(
    ecs_world_t *world,
    const char *button_path,
    const char *building_path)
{
    ecs_entity_t button = ecs_lookup(world, button_path);
    if (!button || ecs_has(world, button, FlecsUiEventListener)) {
        return;
    }

    ecs_entity_t building = ecs_lookup(world, building_path);
    if (!building) {
        return;
    }

    ecs_set(world, button, FlecsUiEventListener, {
        .on_lmb_up = biome_ui_toolButtonUp,
        .ctx = (void*)(uintptr_t)building
    });
}

void BiomeUiBind(ecs_iter_t *it) {
    biome_ui_bindToolButton(it->world, "hud.toolbar.base", "buildings.Base");
    biome_ui_bindToolButton(it->world, "hud.toolbar.solar", "buildings.Solar");
    biome_ui_bindToolButton(it->world, "hud.toolbar.drill", "buildings.Drill");
    biome_ui_bindToolButton(it->world, "hud.toolbar.depot", "buildings.Depot");
    biome_ui_bindToolButton(it->world, "hud.toolbar.habitat", "buildings.Habitat");
    biome_ui_bindToolButton(it->world, "hud.toolbar.lights", "buildings.Lights");
    biome_ui_bindToolButton(it->world, "hud.toolbar.biome", "buildings.Biome");
    biome_ui_bindToolButton(it->world, "hud.toolbar.wire", "buildings.ElectricityPole");
    biome_ui_bindDozeButton(it->world, "hud.toolbar.doze");
}

void biomeUiImport(ecs_world_t *world) {
    ECS_MODULE(world, biomeUi);

    ECS_SYSTEM(world, BiomeUiBind, EcsOnUpdate, 0);
}
