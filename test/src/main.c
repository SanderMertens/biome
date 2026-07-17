
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
    }
};

static bake_test_suite suites[] = {
    {
        "Logistics",
        NULL,
        NULL,
        6,
        Logistics_testcases
    }
};

int main(int argc, char *argv[]) {
    return bake_test_run("biome_test", argc, argv, suites, 1);
}
