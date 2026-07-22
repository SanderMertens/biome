
/* A friendly warning from bake.test
 * ----------------------------------------------------------------------------
 * This file is generated. To add/remove testcases modify the 'project.json' of
 * the test project. ANY CHANGE TO THIS FILE IS LOST AFTER (RE)BUILDING!
 * ----------------------------------------------------------------------------
 */

#include <biome_test.h>

// Testsuite 'Terrain'
void Terrain_regenerate_moisture(void);
void Terrain_spread_colors_across_frames(void);

// Testsuite 'Weather'
void Weather_global_temperature(void);
void Weather_water_state(void);

// Testsuite 'Logistics'
void Logistics_first_come_first_serve(void);
void Logistics_combine_requests(void);
void Logistics_track_outstanding_requests(void);
void Logistics_combine_pickup_requests(void);
void Logistics_combine_dropoff_requests(void);
void Logistics_combine_matching_resources_only(void);
void Logistics_dispatch_finishes_iterator(void);
void Logistics_different_power_network(void);
void Logistics_player_storage_transfer(void);
void Logistics_player_storage_capacity(void);
void Logistics_closest_storage(void);

// Testsuite 'Factory'
void Factory_request_drone_amount(void);
void Factory_request_drone_amount_edge_cases(void);
void Factory_combine_outstanding_requests(void);
void Factory_vent_greenhouse_gas(void);

// Testsuite 'Plant'
void Plant_capture_and_vent(void);
void Plant_capture_requires_gas(void);
void Plant_dies_without_needs(void);
void Plant_fertility_decay(void);
void Plant_spreads_to_neighbors(void);

// Testsuite 'BuildingRule'
void BuildingRule_self_referencing_drag(void);

// Testsuite 'Tool'
void Tool_render_ghost(void);

bake_test_case Terrain_testcases[] = {
    {
        "regenerate_moisture",
        Terrain_regenerate_moisture
    },
    {
        "spread_colors_across_frames",
        Terrain_spread_colors_across_frames
    }
};

bake_test_case Weather_testcases[] = {
    {
        "global_temperature",
        Weather_global_temperature
    },
    {
        "water_state",
        Weather_water_state
    }
};

bake_test_case Logistics_testcases[] = {
    {
        "first_come_first_serve",
        Logistics_first_come_first_serve
    },
    {
        "combine_requests",
        Logistics_combine_requests
    },
    {
        "track_outstanding_requests",
        Logistics_track_outstanding_requests
    },
    {
        "combine_pickup_requests",
        Logistics_combine_pickup_requests
    },
    {
        "combine_dropoff_requests",
        Logistics_combine_dropoff_requests
    },
    {
        "combine_matching_resources_only",
        Logistics_combine_matching_resources_only
    },
    {
        "dispatch_finishes_iterator",
        Logistics_dispatch_finishes_iterator
    },
    {
        "different_power_network",
        Logistics_different_power_network
    },
    {
        "player_storage_transfer",
        Logistics_player_storage_transfer
    },
    {
        "player_storage_capacity",
        Logistics_player_storage_capacity
    },
    {
        "closest_storage",
        Logistics_closest_storage
    }
};

bake_test_case Factory_testcases[] = {
    {
        "request_drone_amount",
        Factory_request_drone_amount
    },
    {
        "request_drone_amount_edge_cases",
        Factory_request_drone_amount_edge_cases
    },
    {
        "combine_outstanding_requests",
        Factory_combine_outstanding_requests
    },
    {
        "vent_greenhouse_gas",
        Factory_vent_greenhouse_gas
    }
};

bake_test_case Plant_testcases[] = {
    {
        "capture_and_vent",
        Plant_capture_and_vent
    },
    {
        "capture_requires_gas",
        Plant_capture_requires_gas
    },
    {
        "dies_without_needs",
        Plant_dies_without_needs
    },
    {
        "fertility_decay",
        Plant_fertility_decay
    },
    {
        "spreads_to_neighbors",
        Plant_spreads_to_neighbors
    }
};

bake_test_case BuildingRule_testcases[] = {
    {
        "self_referencing_drag",
        BuildingRule_self_referencing_drag
    }
};

bake_test_case Tool_testcases[] = {
    {
        "render_ghost",
        Tool_render_ghost
    }
};

static bake_test_suite suites[] = {
    {
        "Terrain",
        NULL,
        NULL,
        2,
        Terrain_testcases
    },
    {
        "Weather",
        NULL,
        NULL,
        2,
        Weather_testcases
    },
    {
        "Logistics",
        NULL,
        NULL,
        11,
        Logistics_testcases
    },
    {
        "Factory",
        NULL,
        NULL,
        4,
        Factory_testcases
    },
    {
        "Plant",
        NULL,
        NULL,
        5,
        Plant_testcases
    },
    {
        "BuildingRule",
        NULL,
        NULL,
        1,
        BuildingRule_testcases
    },
    {
        "Tool",
        NULL,
        NULL,
        1,
        Tool_testcases
    }
};

int main(int argc, char *argv[]) {
    return bake_test_run("biome_test", argc, argv, suites, 7);
}
