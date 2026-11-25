#include "check.h"

int g_failures;
int g_checks;

int main(void)
{
    run_render_tests();
    run_event_tests();
    run_scheduler_tests();
    run_led_tests();
    printf("\n%d checks, %d failed\n", g_checks, g_failures);
    return g_failures ? 1 : 0;
}
