/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (c) 2022 Meta Platforms, Inc. and affiliates.
 * Copyright (c) 2022 Tejun Heo <tj@kernel.org>
 * Copyright (c) 2022 David Vernet <dvernet@meta.com>
 */
#define _GNU_SOURCE
#include <sched.h>
#include <stdio.h>
#include <unistd.h>
#include <inttypes.h>
#include <signal.h>
#include <libgen.h>
#include <bpf/bpf.h>
#include <scx/common.h>
#include "scx_sjf.bpf.skel.h"

const char help_fmt[] =
"A shortest job first sched_ext scheduler.\n"
"\n"
"See the top-level comment in .bpf.c for more details.\n"
"\n"
"Usage: %s [-s SLICE_US] [-c CPU]\n"
"\n"
"  -s SLICE_US   Override slice duration\n"
"  -c CPU        Override the central CPU (default: 0)\n"
"  -v            Print libbpf debug messages\n"
"  -h            Display this help and exit\n";

static bool verbose;
static volatile int exit_req;

static int libbpf_print_fn(enum libbpf_print_level level, const char *format, va_list args)
{
	if (level == LIBBPF_DEBUG && !verbose)
		return 0;
	return vfprintf(stderr, format, args);
}

static void sigint_handler(int dummy)
{
	exit_req = 1;
}

static void read_stats(struct scx_sjf *skel, __u64 *stats)
{
	int nr_cpus = libbpf_num_possible_cpus();
	int num_entries = 4;
	__u64 cnts[num_entries][nr_cpus];
	__u32 idx;

	memset(stats, 0, sizeof(stats[0]) * num_entries);

	for (idx = 0; idx < num_entries; idx++) {
		int ret, cpu;

		ret = bpf_map_lookup_elem(bpf_map__fd(skel->maps.stats),
					  &idx, cnts[idx]);
		if (ret < 0)
			continue;
		for (cpu = 0; cpu < nr_cpus; cpu++)
			stats[idx] += cnts[idx][cpu];
	}
}

int main(int argc, char **argv)
{
    struct scx_sjf * skel;
    struct bpf_link * link;
	__u32 opt;
	__u64 ecode;

	libbpf_set_print(libbpf_print_fn);
	signal(SIGINT, sigint_handler);
	signal(SIGTERM, sigint_handler);
restart:
	skel = SCX_OPS_OPEN(sjf_ops, scx_sjf);

	// Set all variables from BPF file via skeleton here
	// skel->rodata->slice_ns ** Maybe set the time slice here?
	skel->rodata->cpu_num = 3;

	while ((opt = getopt(argc, argv, "c:p")) != -1) {
		switch (opt) {
			case 'c':
				skel->rodata->cpu_num = strtoul(optarg, NULL, 0);
				break;
			case 'p':
				skel->struct_ops.sjf_ops->flags |= SCX_OPS_SWITCH_PARTIAL;
				break;
			default:
				fprintf(stderr, help_fmt, basename(argv[0]));
				return opt != 'h';
		}
	}

	SCX_OPS_LOAD(skel, sjf_ops, scx_sjf, uei);
	link = SCX_OPS_ATTACH(skel, sjf_ops, scx_sjf);

	while (!exit_req && !UEI_EXITED(skel, uei)) {
		__u64 stats[2];

		read_stats(skel, stats);
		printf("started=%llu stopped=%llu dispatched=%llu switches=%llu\n", stats[0], stats[1], stats[2], stats[3]);
		sleep(1);
	}

	bpf_link__destroy(link);
	ecode = UEI_REPORT(skel, uei);
	scx_sjf__destroy(skel);

	if (UEI_ECODE_RESTART(ecode)) {
		goto restart;
	}
	return 0;
}