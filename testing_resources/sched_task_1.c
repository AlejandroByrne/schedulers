/*
 * To compile: 
 *   gcc -Wall -o dummy_task dummy_task.c
 * 
 * This program creates a CPU-bound loop that should run for ~5 seconds,
 * then uses sched_setscheduler() to switch the process to sched-ext (policy 9).
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sched.h>
#include <time.h>
#include <errno.h>

#define SCHED_EXT 7  /* SCHED_EXT is typically identified by 7 */

int main(void) {
    struct sched_param param;
    sched_getparam(0, &param);

    /* Switch this process to sched-ext. If successful, 
     * the kernel's sched_ext framework will schedule this task
     * under whichever BPF scheduler is currently attached.
     */
    if (sched_setscheduler(0, SCHED_EXT, &param) < 0) {
        fprintf(stderr, "errno=%d", errno);
        perror("sched_setscheduler");
        return 1;
    }

    printf("Dummy task started under sched-ext (policy 7)...\n");

    /* Perform a CPU-bound operation for ~5 seconds.
     * NOTE: The exact runtime may vary based on your CPU and compiler optimizations.
     *       We use a loop that does floating-point arithmetic to avoid easy optimization.
     */
    volatile double x = 0.0;
    const unsigned long long loop_count = 800000000ULL;  /* Adjust to tune runtime */

    for (unsigned long long i = 1; i < loop_count; i++) {
        x += 1.0 / (double)i;
        /* Optionally, do something like a mod check to reduce optimization further */
        if ((i % 50000000ULL) == 0) {
            printf("Progress update: i = %llu\n", i);
        }
    }

    printf("Dummy task finished. Result of computation: %f\n", x);
    return 0;
}
