# schedulers


Notes on scx_sjf:
- Implementation guide:
- Each task will be defined with a pre-determined 'estimated_total_run_time'.
- Every time a task starts and stops running, that execution time will be deducted from the
estimated value, and an 'estimated_remaining_run_time' will be calculated. This will serve
as that task's vtime (virtual time).
- Scheduler will have two global (shared across CPUs) dispatch queues, call these DSQ_1 and DSQ_2
    - They will both be vtime ordered queues. This means each time a task is added to a queue, it will be placed in order of that task's vtime (this is the shortest job first logic).
- Each time a CPU's local run queue is empty, it will grab a task (scx_bpf_consume()) from
one of these dispatch queues. The reason there are two is to avoid starvation of the longer jobs. Whether CPUs consume from DSQ_1 or DSQ_2 depends on a switch-like behavior. Consider this scenario:

There are 5 tasks in DSQ_1. This means that currently, all CPUs (let's say there is 1) will take from DSQ_1 when looking for a task to run. When the task stops running (either it finished, or a custom-set time slice runs out), the task (if not finished), will be sent to DSQ_2 -- in order of remaining run time -- to wait for execution. Once DSQ_1 becomes empty, then the "switch" will be flipped, and all CPUs will consume from DSQ_2, and when stopped, all tasks will be sent to DSQ_1. This ensures the order of SJF while still treating all tasks in a round-robin fashion, avoiding starvation of longer tasks.