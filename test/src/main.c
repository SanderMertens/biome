
#include <biome_test.h>

void Logistics_first_come_first_serve(void);
void Logistics_combine_requests(void);
void Logistics_track_outstanding_requests(void);
void Logistics_combine_pickup_requests(void);
void Logistics_combine_dropoff_requests(void);
void Logistics_combine_matching_resources_only(void);
void Logistics_dispatch_finishes_iterator(void);

void Factory_request_drone_amount(void);
void Factory_request_drone_amount_edge_cases(void);

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

static bake_test_suite suites[] = {
    {
        "Logistics",
        NULL,
        NULL,
        7,
        Logistics_testcases
    },
    {
        "Factory",
        NULL,
        NULL,
        2,
        Factory_testcases
    }
};

int main(int argc, char *argv[]) {
    return bake_test_run("biome_test", argc, argv, suites, 2);
}
