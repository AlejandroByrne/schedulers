/*
Shortest Job First (sjf) scheduler.

This current version (v0.1) of the scheduler makes the following assumptions:
1) The SCX_OPS_SWITCH_PARTIAL flag is set to ensure that only tasks that have been
explicitly set to switch to sched-ext will be. Therefore, all tasks that enter the
scheduler will be started for the first time. In other words, all tasks start
with a total execution time of 0, starting from scratch.
2) Only one CPU will be used. By default this is 3, but it can be overridden if
the -c flag is passed (see the runner C file).
3) There is a timer interrupt that allows each task to be run for at most
SCX_SLICE_DFL. This will be changed in the future to be set to a custom value, and
possibly to SCX_SLICE_INF for a "tickless" scheduler.

***NOTE***
1) As of version (v0.1), only run the scheduler with the switch partial (-p) flag,
otherwise too many tasks will overload one CPU and the scheduler will be very slow.
*/

#include <scx/common.bpf.h>

char _license[] SEC("license") = "GPL";

const volatile s32 cpu_num;

struct job_info{
    u64 predicted_run_time;
    u64 last_start_ns;
};

static u64 vtime_now;

UEI_DEFINE(uei);

struct {
    __uint(type, BPF_MAP_TYPE_HASH);
    __uint(max_entries, 4096);
    __uint(key_size, sizeof(char[TASK_COMM_LEN]));
    __uint(value_size, sizeof(struct job_info));
} predicted_times SEC(".maps");

struct {
    __uint(type, BPF_MAP_TYPE_ARRAY); // since only one CPU is being used in version (v0.1), this should be okay
    __uint(max_entries, 2); // [num_starts, num_stops]
    __uint(key_size, sizeof(u32));
    __uint(value_size, sizeof(u64));
} stats SEC(".maps");

enum {
    DSQ_1 = 101,
    DSQ_2 = 102,
};

// Global flag to track which DSQ is currently being pulled from or discarded to
bool use_dsq_1 = true;

static void stat_inc(u32 idx)
{
	u64 *cnt_p = bpf_map_lookup_elem(&stats, &idx);
	if (cnt_p)
		(*cnt_p)++;
}

static inline bool vtime_before(u64 a, u64 b)
{
	return (s64)(a - b) < 0;
}

s32 BPF_STRUCT_OPS(sjf_select_cpu, struct task_struct *p, s32 prev_cpu, u64 wake_flags) {
    return cpu_num;
}

void BPF_STRUCT_OPS(sjf_enqueue, struct task_struct *p, u64 enq_flags) {
    u64 vtime = p->scx.dsq_vtime;

    // Calculate the remaining time and use that as vtime

    // If not using/"consuming" to DSQ_1, discard to DSQ_1 instead
    scx_bpf_dispatch_vtime(p, !use_dsq_1 ? DSQ_1 : DSQ_2, SCX_SLICE_INF, vtime, enq_flags);
}

void BPF_STRUCT_OPS(sjf_dispatch, s32 cpu, struct task_struct *prev)
{
    // If using/"consuming" from DSQ_1, then consume from DSQ_1
	scx_bpf_consume(use_dsq_1 ? DSQ_1 : DSQ_2);
}

void BPF_STRUCT_OPS(sjf_running, struct task_struct *p) {
    stat_inc(0);
    /*
	 * Global vtime always progresses forward as tasks start executing. The
	 * test and update can be performed concurrently from multiple CPUs and
	 * thus racy. Any error should be contained and temporary. Let's just
	 * live with it.
	 */
	if (vtime_before(vtime_now, p->scx.dsq_vtime))
		vtime_now = p->scx.dsq_vtime;
}

void BPF_STRUCT_OPS(sjf_stopping, struct task_struct *p, bool runnable) {
    stat_inc(1);
    p->scx.dsq_vtime += (SCX_SLICE_DFL - p->scx.slice);
}

void BPF_STRUCT_OPS(sjf_enable, struct task_struct *p)
{
	p->scx.dsq_vtime = vtime_now;
}

s32 BPF_STRUCT_OPS_SLEEPABLE(sjf_init)
{
	int ret;

    ret = scx_bpf_create_dsq(DSQ_1, -1);
    if (ret)
        return ret;
    ret = scx_bpf_create_dsq(DSQ_2, -1);
    if (ret)
        return ret;

    return 0;
}

void BPF_STRUCT_OPS(sjf_exit, struct scx_exit_info *ei)
{
	UEI_RECORD(uei, ei);
}

SCX_OPS_DEFINE(sjf_ops,
	       .select_cpu		= (void *)sjf_select_cpu,
	       .enqueue			= (void *)sjf_enqueue,
	       .dispatch		= (void *)sjf_dispatch,
	       .running			= (void *)sjf_running,
	       .stopping		= (void *)sjf_stopping,
	       .enable			= (void *)sjf_enable,
	       .init			= (void *)sjf_init,
	       .exit			= (void *)sjf_exit,
	       .name			= "sjf");