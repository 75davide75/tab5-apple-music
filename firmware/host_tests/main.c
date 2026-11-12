#include <stdio.h>
int g_failures, g_checks;
void run_ams_parse_tests(void);
void run_playback_clock_tests(void);
void run_standby_tests(void);
void run_source_arbiter_tests(void);
void run_text_util_tests(void);
void run_lyrics_tests(void);

int main(void)
{
    run_ams_parse_tests();
    run_playback_clock_tests();
    run_standby_tests();
    run_source_arbiter_tests();
    run_text_util_tests();
    run_lyrics_tests();
    printf("%d checks, %d failures\n", g_checks, g_failures);
    return g_failures ? 1 : 0;
}
