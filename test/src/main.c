
/* A friendly warning from bake.test
 * ----------------------------------------------------------------------------
 * This file is generated. To add/remove testcases modify the 'project.json' of
 * the test project. ANY CHANGE TO THIS FILE IS LOST AFTER (RE)BUILDING!
 * ----------------------------------------------------------------------------
 */

#include <biome_test.h>

// Testsuite 'Logistics'
void Logistics_first_come_first_serve(void);
void Logistics_combine_requests(void);
void Logistics_track_outstanding_requests(void);
void Logistics_combine_pickup_requests(void);
void Logistics_combine_dropoff_requests(void);
void Logistics_combine_matching_resources_only(void);
void Logistics_dispatch_finishes_iterator(void);
void Logistics_player_storage_transfer(void);
void Logistics_player_storage_capacity(void);
void Logistics_closest_storage(void);

// Testsuite 'Factory'
void Factory_request_drone_amount(void);
void Factory_request_drone_amount_edge_cases(void);

// Testsuite 'BuildingRule'
void BuildingRule_self_referencing_drag(void);

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
    }
};

bake_test_case BuildingRule_testcases[] = {
    {
        "self_referencing_drag",
        BuildingRule_self_referencing_drag
    }
};

static bake_test_suite suites[] = {
    {
        "Logistics",
        NULL,
        NULL,
        10,
        Logistics_testcases
    },
    {
        "Factory",
        NULL,
        NULL,
        2,
        Factory_testcases
    },
    {
        "BuildingRule",
        NULL,
        NULL,
        1,
        BuildingRule_testcases
    }
};

int main(int argc, char *argv[]) {
    return bake_test_run("biome_test", argc, argv, suites, 3);
}
