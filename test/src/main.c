#include <biome_test.h>

void Logistics_first_come_first_serve(void);

bake_test_case Logistics_testcases[] = {
    {
        "first_come_first_serve",
        Logistics_first_come_first_serve
    }
};

static bake_test_suite suites[] = {
    {
        "Logistics",
        NULL,
        NULL,
        1,
        Logistics_testcases
    }
};

int main(int argc, char *argv[]) {
    return bake_test_run("biome_test", argc, argv, suites, 1);
}
