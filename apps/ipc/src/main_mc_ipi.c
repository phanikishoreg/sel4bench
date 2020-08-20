#include <ipc.h>
#ifdef MC_IPI_IPC
/*
 * Copyright 2017, Data61
 * Commonwealth Scientific and Industrial Research Organisation (CSIRO)
 * ABN 41 687 119 230.
 *
 * This software may be distributed and modified according to the terms of
 * the BSD 2-Clause license. Note that NO WARRANTY is provided.
 * See "LICENSE_BSD2.txt" for details.
 *
 * @TAG(DATA61_BSD)
 */
/* This is very much a work in progress IPC benchmarking set. Goal is
   to eventually use this to replace the rest of the random benchmarking
   happening in this app with just what we need */

#include <autoconf.h>
#include <allocman/vka.h>
#include <allocman/bootstrap.h>
#include <stdbool.h>
#include <stdio.h>
#include <stddef.h>
#include <stdlib.h>
#include <stdarg.h>
#include <sel4/sel4.h>
#include <sel4bench/sel4bench.h>
#include <sel4utils/process.h>
#include <string.h>
#include <utils/util.h>
#include <vka/vka.h>

#include <benchmark.h>

/* arch/ipc.h requires these defines */
#define NOPS ""

#include <arch/ipc.h>

#define NUM_ARGS 3
#define WARMUPS RUNS
#define OVERHEAD_RETRIES 4

#ifndef CONFIG_CYCLE_COUNT

#define GENERIC_COUNTER_MASK (BIT(0))
#undef READ_COUNTER_BEFORE
#undef READ_COUNTER_AFTER
#define READ_COUNTER_BEFORE(x) sel4bench_get_counters(GENERIC_COUNTER_MASK, &x);
#define READ_COUNTER_AFTER(x) sel4bench_get_counters(GENERIC_COUNTER_MASK, &x)
#endif

typedef struct helper_thread {
	sel4utils_process_t process;
	seL4_CPtr ep;
	seL4_CPtr result_ep;
	char *argv[NUM_ARGS];
	char argv_strings[NUM_ARGS][WORD_STRING_SIZE];
} helper_thread_t;

void
abort(void)
{
	benchmark_finished(EXIT_FAILURE);
}

size_t __arch_write(char *data, int count)
{
	return benchmark_write(data, count);
}

static void
timing_init(void)
{
	sel4bench_init();
#ifdef CONFIG_GENERIC_COUNTER
	event_id_t event = GENERIC_EVENTS[CONFIG_GENERIC_COUNTER_ID];
	sel4bench_set_count_event(0, event);
	sel4bench_reset_counters();
	sel4bench_start_counters(GENERIC_COUNTER_MASK);
#endif
}

void
timing_destroy(void)
{
#ifdef CONFIG_GENERIC_COUNTER
	sel4bench_stop_counters(GENERIC_COUNTER_MASK);
	sel4bench_destroy();
#endif
}

static inline void
dummy_seL4_Send(seL4_CPtr ep, seL4_MessageInfo_t tag)
{
	(void)ep;
	(void)tag;
}

static inline void
dummy_seL4_Call(seL4_CPtr ep, seL4_MessageInfo_t tag)
{
	(void)ep;
	(void)tag;
}

static inline void
dummy_seL4_Reply(UNUSED seL4_CPtr reply, seL4_MessageInfo_t tag)
{
	(void)tag;
}

seL4_Word ipc_call_func(int argc, char *argv[]);
seL4_Word ipc_call_func2(int argc, char *argv[]);
seL4_Word ipc_call_10_func(int argc, char *argv[]);
seL4_Word ipc_call_10_func2(int argc, char *argv[]);
seL4_Word ipc_replyrecv_func2(int argc, char *argv[]);
seL4_Word ipc_replyrecv_func(int argc, char *argv[]);
seL4_Word ipc_replyrecv_10_func2(int argc, char *argv[]);
seL4_Word ipc_replyrecv_10_func(int argc, char *argv[]);
seL4_Word ipc_send_func(int argc, char *argv[]);
seL4_Word ipc_recv_func(int argc, char *argv[]);

static helper_func_t bench_funcs[] = {
	ipc_call_func,
	ipc_call_func2,
	ipc_call_10_func,
	ipc_call_10_func2,
	ipc_replyrecv_func2,
	ipc_replyrecv_func,
	ipc_replyrecv_10_func2,
	ipc_replyrecv_10_func,
	ipc_send_func,
	ipc_recv_func
};

#define IPC_CALL_FUNC(name, bench_func, send_func, call_func, send_start_end, length) \
	seL4_Word name(int argc, char *argv[]) { \
		uint32_t i; \
		ccnt_t start UNUSED, end UNUSED; \
		seL4_CPtr ep = atoi(argv[0]);\
		seL4_CPtr result_ep = atoi(argv[1]);\
		seL4_MessageInfo_t tag = seL4_MessageInfo_new(0, 0, 0, length); \
		call_func(ep, tag); \
		COMPILER_MEMORY_FENCE(); \
		for (i = 0; i < WARMUPS; i++) { \
			READ_COUNTER_BEFORE(start); \
			bench_func(ep, tag); \
			READ_COUNTER_AFTER(end); \
		} \
		COMPILER_MEMORY_FENCE(); \
		send_result(result_ep, send_start_end); \
		send_func(ep, tag); \
		api_wait(ep, NULL);/* block so we don't run off the stack */ \
		return 0; \
	}

IPC_CALL_FUNC(ipc_call_func, DO_REAL_CALL, seL4_Send, dummy_seL4_Call, end, 0)
IPC_CALL_FUNC(ipc_call_func2, DO_REAL_CALL, dummy_seL4_Send, seL4_Call, start, 0)
IPC_CALL_FUNC(ipc_call_10_func, DO_REAL_CALL_10, seL4_Send, dummy_seL4_Call, end, 10)
IPC_CALL_FUNC(ipc_call_10_func2, DO_REAL_CALL_10, dummy_seL4_Send, seL4_Call, start, 10)

#define IPC_REPLY_RECV_FUNC(name, bench_func, reply_func, recv_func, send_start_end, length) \
		seL4_Word name(int argc, char *argv[]) { \
			uint32_t i; \
			ccnt_t start UNUSED, end UNUSED; \
			seL4_MessageInfo_t tag = seL4_MessageInfo_new(0, 0, 0, length); \
			seL4_CPtr ep = atoi(argv[0]);\
			seL4_CPtr result_ep = atoi(argv[1]);\
			seL4_CPtr reply = atoi(argv[2]);\
			if (config_set(CONFIG_KERNEL_RT)) {\
				api_nbsend_recv(ep, tag, ep, NULL, reply);\
			} else {\
				recv_func(ep, NULL, reply); \
			}\
			COMPILER_MEMORY_FENCE(); \
			for (i = 0; i < WARMUPS; i++) { \
				READ_COUNTER_BEFORE(start); \
				bench_func(ep, tag, reply); \
				READ_COUNTER_AFTER(end); \
			} \
			COMPILER_MEMORY_FENCE(); \
			reply_func(reply, tag); \
			send_result(result_ep, send_start_end); \
			api_wait(ep, NULL); /* block so we don't run off the stack */ \
			return 0; \
		}

IPC_REPLY_RECV_FUNC(ipc_replyrecv_func2, DO_REAL_REPLY_RECV, api_reply, api_recv, end, 0)
IPC_REPLY_RECV_FUNC(ipc_replyrecv_func, DO_REAL_REPLY_RECV, dummy_seL4_Reply, api_recv, start, 0)
IPC_REPLY_RECV_FUNC(ipc_replyrecv_10_func2, DO_REAL_REPLY_RECV_10, api_reply, api_recv, end, 10)
IPC_REPLY_RECV_FUNC(ipc_replyrecv_10_func, DO_REAL_REPLY_RECV_10, dummy_seL4_Reply, api_recv, start, 10)

seL4_Word
ipc_recv_func(int argc, char *argv[])
{
	uint32_t i;
	ccnt_t start UNUSED, end UNUSED;
	seL4_CPtr ep = atoi(argv[0]);
	seL4_CPtr result_ep = atoi(argv[1]);
	UNUSED seL4_CPtr reply = atoi(argv[2]);

	COMPILER_MEMORY_FENCE();
	for (i = 0; i < WARMUPS; i++) {
		READ_COUNTER_BEFORE(start);
		DO_REAL_RECV(ep, reply);
		READ_COUNTER_AFTER(end);
	}
	COMPILER_MEMORY_FENCE();
	DO_REAL_RECV(ep, reply);
	send_result(result_ep, end);
	return 0;
}

seL4_Word
ipc_send_func(int argc, char *argv[])
{
	uint32_t i;
	ccnt_t start UNUSED, end UNUSED;
	seL4_CPtr ep = atoi(argv[0]);
	seL4_CPtr result_ep = atoi(argv[1]);
	seL4_MessageInfo_t tag = seL4_MessageInfo_new(0, 0, 0, 0);
	COMPILER_MEMORY_FENCE();
	for (i = 0; i < WARMUPS; i++) {
		READ_COUNTER_BEFORE(start);
		DO_REAL_SEND(ep, tag);
		READ_COUNTER_AFTER(end);
	}
	COMPILER_MEMORY_FENCE();
	send_result(result_ep, start);
	DO_REAL_SEND(ep, tag);
	return 0;
}

#define N_THD_ARGS 3
#define NCORES 6
#define NTHDS 4
#undef DO_NB_SEND
#define COREN_THD_ARGS (NTHDS+N_THD_ARGS)
#define CPU_CYCS_PER_US 3200ULL //is there a sel4 api for that?

#define MEASURE_DL
#ifndef MEASURE_DL
//#define MYDEADLINE_FIXED ((uint64_t)(1000ULL * 1000ULL * 3200ULL)) //3200cycs per usec on hw, 1000usecs == 1ms
#define WINDOW_US 10000 //10ms
#define PRINT_US  1000000 //1s
#define WINDOW_SZ (PRINT_US/WINDOW_US)
#define IPI_MIN_THRESH 300

volatile unsigned long ipi_rate[WINDOW_SZ] = { 0 }, ipi_winidx = 0, iters = 0;
volatile uint64_t ipi_win_st = 0, ipi_print_st = 0, win_cycs = CPU_CYCS_PER_US * WINDOW_US, print_cycs = CPU_CYCS_PER_US * PRINT_US;
#else

/*
 * NOTE: Run spinlib in composite on baremetal with no timers, etc (micro_booter level),
 * get the number of iterations to spin for a given number of micro-seconds..
 * use that value here.. do the same for all envs (linux, cos, fiasco, and seL4).
 */
#define ITERS_10US 5850
#define MULTIPLE   1

#define SPIN_ITERS (ITERS_10US*MULTIPLE) //note
#define NITERS     100

volatile uint64_t spin_cycs[NITERS] = { 0ULL }; /* time spent in spin per iteration */
volatile uint32_t niters = 0;

static void __spin_fn(void) __attribute__ ((optimize("O0")));

static void
__spin_fn(void)
{
        unsigned int spin = 0;

        while (spin < SPIN_ITERS) {
                __asm__ __volatile__("nop": : :"memory");
                spin++;
        }
}
#endif
static volatile int start_test = 0;

#define MYPRINT_MAX_LEN 128

static void
myprint(char *fmt, ...)
{
	char s[MYPRINT_MAX_LEN] = { 0 };
	va_list arg_ptr;
	ssize_t ret, len = MYPRINT_MAX_LEN;

	va_start(arg_ptr, fmt);
	ret = vsnprintf(s, len, fmt, arg_ptr);
	va_end(arg_ptr);

	/*
	 * Don't use printf because it's a SYNC call to a print thread and
	 * that means, calling thread blocks until the print thread serves it's request.
	 * Why we don't want that is because, if a high prio thread calls printf and blocks,
	 * that lets the low prio threads run and we don't want that!!!
	 *
	 * Unfortunately, there is no "puts" kinda functionality.. So we have to live with the
	 * performance issue of calling a "system call" multiple times on each string!
	 * Why we don't care about that perf is because this is not blocking semantics..
	 * Rather it is a "CPU-bound" work, which we are fine having in our high-prio thread!
	 *
	 * What this also means is that, we need to enable CONFIG_PRINTING and hope that there
	 * are no prints in the kernel during our measurements!
	 */
	if (config_set(CONFIG_PRINTING)) {
		int i = 0;

		for (i = 0; i < ret; i++) seL4_DebugPutChar(s[i]);
	} else {
		printf("%s", s);
	}
}

static volatile uint32_t nsnd_counter, nrcv_counter;
#ifndef DO_NB_SEND
static sel4utils_thread_t hithd, rcvthd[NCORES-1][NTHDS], sndthd[NCORES-1][NTHDS];
#else
static sel4utils_thread_t hithd, rcvthd[NTHDS], sndthd[NCORES-1];
#endif

static void
myhiprio_fn(int argc, char **argv)
{
//	volatile uint64_t st = 0, end = 0, deadline = MYDEADLINE_FIXED;
	seL4_CPtr ep = (seL4_CPtr) atol(argv[0]);
	seL4_CPtr hep = (seL4_CPtr) atol(argv[1]);
	uint32_t id = (uint32_t)atol(argv[2]);

#ifndef MEASURE_DL
	uint64_t now, prev, wc = 0;
	now = prev = sel4bench_get_rdtsc();
	start_test = 1;
	seL4_Yield();
	/* I added sel4bench_get_rdtsc() - Phani */
//	st = end = sel4bench_get_rdtsc();
//	deadline += st;

	while (1) {
		uint64_t diff;

		now = sel4bench_get_rdtsc();
		diff = now - prev;
		if (diff > wc) wc = diff;
		if (diff > IPI_MIN_THRESH) ipi_rate[ipi_winidx]++;

		if (now - ipi_win_st >= win_cycs) {
			if (ipi_winidx < WINDOW_SZ - 1) {
				ipi_winidx++;
				ipi_rate[ipi_winidx] = 0;
			}
			ipi_win_st = now;
		}
		if (now - ipi_print_st >= print_cycs) {
			unsigned long i, tot = 0;

			for (i = 0; i < ipi_winidx; i++) {
				tot += ipi_rate[i];
				myprint("%lu, ", ipi_rate[i]);
			}
			myprint("[%lu, %llu]\n", tot, wc);
			ipi_winidx = 0;
			ipi_rate[ipi_winidx] = 0;
			now = sel4bench_get_rdtsc();
			wc = 0;
			iters++;
			ipi_win_st = ipi_print_st = now;
		}
		prev = now;
	}
#else
	while (niters < NITERS) {
		unsigned int spin = 0;
		uint64_t st = sel4bench_get_rdtsc(), en = 0;

		__spin_fn();
		en = sel4bench_get_rdtsc();

		spin_cycs[niters] = en - st;
		niters++;
	}

	int i;
	for (i = 0; i < NITERS; i++) {
		myprint("%llu\n", spin_cycs[i]);
	}
	myprint("----------------------------\n");
#endif
	seL4_Send(ep, seL4_MessageInfo_new(0, 0, 0, 0));
}

static void
myrecv_fn(int argc, char *argv[])
{
	uint32_t i;
	ccnt_t start UNUSED, end UNUSED;
	seL4_CPtr ep = atoi(argv[0]);
	UNUSED seL4_CPtr reply = atoi(argv[1]);
	uint32_t id = (uint32_t)atol(argv[2]);

	__sync_fetch_and_add(&nrcv_counter, 1);

	while (1) {
		seL4_Recv(ep, NULL);
	}

	return;
}

static void
mynbsend_fn(int argc, char *argv[])
{
	/* each core has 1 thread sending to N rcv end-points */
	uint32_t i;
	seL4_CPtr ep[COREN_THD_ARGS];
	uint32_t id = (uint32_t)atol(argv[2]);
	seL4_MessageInfo_t tag = seL4_MessageInfo_new(0, 0, 0, 0);

	for (i = N_THD_ARGS; i < COREN_THD_ARGS; i++) {
		ep[i] = atoi(argv[i]);
	}
	__sync_fetch_and_add(&nsnd_counter, 1);
	while (nsnd_counter != NCORES-1) seL4_Yield();
	while (!start_test) seL4_Yield();

	while (1) {
		for (i = N_THD_ARGS; i < COREN_THD_ARGS; i++) {
			seL4_NBSend(ep[i], tag);
		}
	}

	return;
}

static void
mysend_fn(int argc, char *argv[])
{
	uint32_t i;
	ccnt_t start UNUSED, end UNUSED;
	seL4_CPtr ep = atoi(argv[0]);
	seL4_MessageInfo_t tag = seL4_MessageInfo_new(0, 0, 0, 0);
	uint32_t id = (uint32_t)atol(argv[2]);

	__sync_fetch_and_add(&nsnd_counter, 1);
	while (!start_test) seL4_Yield();

	while (1) {
		seL4_Send(ep, tag);
	}

	return;
}

int
main(int argc, char **argv)
{
	int i = 0, j = 0, error;
	env_t *env;
#ifndef DO_NB_SEND
	vka_object_t ep[NCORES-1][NTHDS], result_ep, hep;
#else
	vka_object_t ep[NTHDS], result_ep, hep;
#endif

	static size_t object_freq[seL4_ObjectTypeCount] = {
		[seL4_TCBObject] = 4,
		[seL4_EndpointObject] = 2,
#ifdef CONFIG_KERNEL_RT
		[seL4_SchedContextObject] = 4,
		[seL4_ReplyObject] = 4
#endif
	};

	env = benchmark_get_env(argc, argv, sizeof(ipc_results_t), object_freq);
	ipc_results_t *results = (ipc_results_t *) env->results;

#ifndef DO_NB_SEND
	for (i = 1; i < NCORES; i++) {
		for (j = 0; j < NTHDS; j++) {
			/* allocate benchmark endpoint - the IPC's that we benchmark
			   will be sent over this ep */
			if (vka_alloc_endpoint(&env->slab_vka, &ep[i-1][j]) != 0) {
				ZF_LOGF("Failed to allocate endpoint");
			}
		}
	}
#else
	for (j = 0; j < NTHDS; j++) {
		/* allocate benchmark endpoint - the IPC's that we benchmark
		   will be sent over this ep */
		if (vka_alloc_endpoint(&env->slab_vka, &ep[j]) != 0) {
			ZF_LOGF("Failed to allocate endpoint");
		}
	}
#endif

	/* allocate result ep - the IPC threads will send their timestamps
	   to this ep */
	if (vka_alloc_endpoint(&env->slab_vka, &result_ep) != 0) {
		ZF_LOGF("Failed to allocate endpoint");
	}
	/* allocate result ep - the IPC threads will send their timestamps
	   to this ep */
	if (vka_alloc_endpoint(&env->slab_vka, &hep) != 0) {
		ZF_LOGF("Failed to allocate endpoint");
	}

	if (config_set(CONFIG_PRINTING)) printf("seL4_DebugPutChar will work!\n");
	else                             printf("seL4_DebugPutChar is disabled\n");

	char hithd_args_strings[N_THD_ARGS][WORD_STRING_SIZE];
	char *hithd_argv[N_THD_ARGS];

	sched_params_t cnparams = { 0 };
	sched_params_t c0params = { 0 };
	c0params.core = 0;
	benchmark_configure_thread(env, result_ep.cptr, seL4_MaxPrio, "hiprio", &hithd);
	sel4utils_create_word_args(hithd_args_strings, hithd_argv, N_THD_ARGS, result_ep.cptr, hep.cptr, 0);
	error = sel4utils_set_sched_affinity(&hithd, c0params);
	assert(!error);

#ifndef DO_NB_SEND
	char rcvthd_args_strings[NCORES - 1][NTHDS][N_THD_ARGS][WORD_STRING_SIZE];
	char sndthd_args_strings[NCORES - 1][NTHDS][N_THD_ARGS][WORD_STRING_SIZE];
	char *rcvthd_argv[NCORES - 1][NTHDS][N_THD_ARGS];
	char *sndthd_argv[NCORES - 1][NTHDS][N_THD_ARGS];

	for (i = 1; i < NCORES; i++) {
		char sname[16] = { 0 }, rname[16] = { 0 };

		for (j = 0; j < NTHDS; j++) {
			cnparams.core = i;
			sprintf(sname, "snd%d_%d_%d", j, i, 0);
			sprintf(rname, "rcv%d_%d_%d", j, 0, i);
			benchmark_configure_thread(env, result_ep.cptr, seL4_MaxPrio-1, rname, &rcvthd[i-1][j]);
			sel4utils_create_word_args(rcvthd_args_strings[i-1][j], rcvthd_argv[i-1][j], N_THD_ARGS, ep[i-1][j].cptr, SEL4UTILS_REPLY_SLOT, i*j);
			error = sel4utils_set_sched_affinity(&rcvthd[i-1][j], c0params);
			assert(!error);

			benchmark_configure_thread(env, result_ep.cptr, seL4_MaxPrio, sname, &sndthd[i-1][j]);
			sel4utils_create_word_args(sndthd_args_strings[i-1][j], sndthd_argv[i-1][j], N_THD_ARGS, ep[i-1][j].cptr, 0, i*j);
			error = sel4utils_set_sched_affinity(&sndthd[i-1][j], cnparams);
			assert(!error);

			sel4utils_start_thread(&rcvthd[i-1][j], (sel4utils_thread_entry_fn)myrecv_fn, (void *)N_THD_ARGS, (void *)rcvthd_argv[i-1][j], 1);
			seL4_Yield();
		}
	}

	for (i = 1; i < NCORES; i++) {
		for (j = 0; j < NTHDS; j++) {
			sel4utils_start_thread(&sndthd[i-1][j], (sel4utils_thread_entry_fn)mysend_fn, (void *)N_THD_ARGS, (void *)sndthd_argv[i-1][j], 1);
			seL4_Yield();
		}
	}
#else
	char rcvthd_args_strings[NTHDS][N_THD_ARGS][WORD_STRING_SIZE];
	char sndthd_args_strings[NCORES - 1][COREN_THD_ARGS][WORD_STRING_SIZE];
	char *rcvthd_argv[NTHDS][N_THD_ARGS];
	char *sndthd_argv[NCORES - 1][COREN_THD_ARGS];

	for (i = 0; i < NTHDS; i++) {
		char rname[16] = { 0 };

		sprintf(rname, "rcv_%d", i);
		benchmark_configure_thread(env, result_ep.cptr, seL4_MaxPrio-1, rname, &rcvthd[i]);
		sel4utils_create_word_args(rcvthd_args_strings[i], rcvthd_argv[i], N_THD_ARGS, ep[i].cptr, SEL4UTILS_REPLY_SLOT, i);
		error = sel4utils_set_sched_affinity(&rcvthd[i], c0params);
		assert(!error);

		sel4utils_start_thread(&rcvthd[i], (sel4utils_thread_entry_fn)myrecv_fn, (void *)N_THD_ARGS, (void *)rcvthd_argv[i], 1);
		seL4_Yield();
	}

	for (i = 1; i < NCORES; i++) {
		char sname[16] = { 0 };

		j = 0;
		cnparams.core = i;
		sprintf(sname, "snd_%d", i);
		benchmark_configure_thread(env, result_ep.cptr, seL4_MaxPrio, sname, &sndthd[i-1]);
		/* using sel4utils_create_word_args body */
		sndthd_argv[i-1][j] = sndthd_args_strings[i-1][j];
		snprintf(sndthd_argv[i-1][j], WORD_STRING_SIZE, "%"PRIuPTR"", 0);
		j++;
		sndthd_argv[i-1][j] = sndthd_args_strings[i-1][j];
		snprintf(sndthd_argv[i-1][j], WORD_STRING_SIZE, "%"PRIuPTR"", 0);
		j++;
		sndthd_argv[i-1][j] = sndthd_args_strings[i-1][j];
		snprintf(sndthd_argv[i-1][j], WORD_STRING_SIZE, "%"PRIuPTR"", i);

		for (j = N_THD_ARGS; j < COREN_THD_ARGS; j++) {
			seL4_Word arg = ep[j-N_THD_ARGS].cptr;

			sndthd_argv[i-1][j] = sndthd_args_strings[i-1][j];
			snprintf(sndthd_argv[i-1][j], WORD_STRING_SIZE, "%"PRIuPTR"", arg);
		}

		error = sel4utils_set_sched_affinity(&sndthd[i-1], cnparams);
		assert(!error);

		sel4utils_start_thread(&sndthd[i-1], (sel4utils_thread_entry_fn)mynbsend_fn, (void *)COREN_THD_ARGS, (void *)sndthd_argv[i-1], 1);
		seL4_Yield();
	}
#endif
	sel4utils_start_thread(&hithd, (sel4utils_thread_entry_fn)myhiprio_fn, (void *)N_THD_ARGS, (void *)hithd_argv, 1);

	benchmark_wait_children(result_ep.cptr, "hiprio", 1);
	seL4_TCB_Suspend(hithd.tcb.cptr);
#ifndef DO_NB_SEND
	for (i = 1; i < NCORES; i ++) {
		for (j = 0; j < NTHDS; j++) {
			seL4_TCB_Suspend(sndthd[i-1][j].tcb.cptr);
			seL4_TCB_Suspend(rcvthd[i-1][j].tcb.cptr);
		}
	}
#else
	for (i = 1; i < NCORES; i ++) {
		seL4_TCB_Suspend(sndthd[i-1].tcb.cptr);
	}

	for (j = 0; j < NTHDS; j++) {
		seL4_TCB_Suspend(rcvthd[j].tcb.cptr);
	}

#endif

	/* done -> results are stored in shared memory so we can now return */
	benchmark_finished(EXIT_SUCCESS);
	return 0;
}
#endif
