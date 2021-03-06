#include "ccv_nnc.h"
#include "ccv_nnc_internal.h"
#include "ccv_nnc_easy.h"
#ifdef HAVE_CUDA
#include "gpu/ccv_nnc_compat.h"
#endif
#include "_ccv_nnc_stream.h"

typedef struct {
	ccv_nnc_stream_context_t super;
	// Left for implementation yet, the CPU support for stream context.
	size_t workspace_size;
	void* workspace;
} ccv_nnc_stream_cpu_t;

ccv_nnc_stream_context_t* ccv_nnc_stream_context_new(const int type)
{
	ccv_nnc_stream_cpu_t* const stream_cpu = (ccv_nnc_stream_cpu_t*)cccalloc(1, sizeof(ccv_nnc_stream_cpu_t));
	stream_cpu->super.type = type;
	stream_cpu->workspace_size = 0;
	stream_cpu->workspace = 0;
#ifdef HAVE_CUDA
	if (CCV_STREAM_GET_CONTEXT(type) == CCV_STREAM_CONTEXT_GPU)
		return ccv_nnc_init_stream_context((ccv_nnc_stream_context_t*)stream_cpu);
#endif
	return (ccv_nnc_stream_context_t*)stream_cpu;
}

#ifndef HAVE_CUDA
static __thread ccv_nnc_stream_cpu_t ccv_nnc_per_thread_stream_cpu = {
	.super = {
		.type = CCV_STREAM_CONTEXT_CPU,
	},
};
#endif

void* ccv_nnc_stream_context_get_workspace(ccv_nnc_stream_context_t* const stream_context, const size_t workspace_size, const int mem)
{
#ifdef HAVE_CUDA
	return ccv_nnc_stream_compat_get_workspace(stream_context, workspace_size, mem);
#else
	ccv_nnc_stream_cpu_t* stream_cpu = (ccv_nnc_stream_cpu_t*)stream_context;
	if (!stream_cpu)
		stream_cpu = &ccv_nnc_per_thread_stream_cpu;
	assert(mem == CCV_TENSOR_CPU_MEMORY);
	if (stream_cpu->workspace_size >= workspace_size)
		return stream_cpu->workspace;
	stream_cpu->workspace_size = workspace_size;
	if (stream_cpu->workspace)
		ccfree(stream_cpu->workspace);
	stream_cpu->workspace = 0;
	ccmemalign(&stream_cpu->workspace, 16, workspace_size);
	return stream_cpu->workspace;
#endif
}

void ccv_nnc_stream_context_drain(ccv_nnc_stream_context_t* const stream_context)
{
#ifdef HAVE_CUDA
	ccv_nnc_stream_compat_drain(stream_context);
#else
	ccv_nnc_stream_cpu_t* stream_cpu = (ccv_nnc_stream_cpu_t*)stream_context;
	if (!stream_cpu)
		stream_cpu = &ccv_nnc_per_thread_stream_cpu;
	if (stream_cpu->workspace)
	{
		ccfree(stream_cpu->workspace);
		stream_cpu->workspace = 0;
		stream_cpu->workspace_size = 0;
	}
#endif
}

void ccv_nnc_stream_context_wait(const ccv_nnc_stream_context_t* const stream_context)
{
	if (!stream_context)
		return;
	ccv_nnc_stream_scheduler_t* const scheduler = stream_context->scheduler;
	if (scheduler) // First wait the scheduler to finish.
	{
		pthread_mutex_lock(&scheduler->mutex);
		while (scheduler->active)
			pthread_cond_wait(&scheduler->notify, &scheduler->mutex);
		pthread_mutex_unlock(&scheduler->mutex);
	}
#ifdef HAVE_CUDA
	if (CCV_STREAM_GET_CONTEXT(stream_context->type) == CCV_STREAM_CONTEXT_GPU)
		ccv_nnc_synchronize_stream_context(stream_context);
#endif
}

void ccv_nnc_stream_context_free(ccv_nnc_stream_context_t* const stream_context)
{
#ifdef HAVE_CUDA
	if (CCV_STREAM_GET_CONTEXT(stream_context->type) == CCV_STREAM_CONTEXT_GPU)
		ccv_nnc_deinit_stream_context(stream_context);
#else
	ccv_nnc_stream_cpu_t* stream_cpu = (ccv_nnc_stream_cpu_t*)stream_context;
	if (stream_cpu->workspace)
		ccfree(stream_cpu->workspace);
#endif
	ccfree(stream_context);
}

ccv_nnc_stream_signal_t* ccv_nnc_stream_signal_new(const int type)
{
	ccv_nnc_stream_signal_t* const signal = (ccv_nnc_stream_signal_t*)ccmalloc(sizeof(ccv_nnc_stream_signal_t));
	signal->type = type;
#ifdef HAVE_CUDA
	if (CCV_STREAM_GET_CONTEXT(type) == CCV_STREAM_CONTEXT_GPU)
		return ccv_nnc_init_stream_signal(signal);
#endif
	return signal;
}

void ccv_nnc_stream_context_emit_signal(const ccv_nnc_stream_context_t* const stream, const ccv_nnc_stream_signal_t* const signal)
{
#ifdef HAVE_CUDA
	if (CCV_STREAM_GET_CONTEXT(signal->type) == CCV_STREAM_CONTEXT_GPU)
		ccv_nnc_stream_compat_emit_signal(stream, signal);
#endif
}

void ccv_nnc_stream_context_wait_signal(const ccv_nnc_stream_context_t* const stream, const ccv_nnc_stream_signal_t* const signal)
{
#ifdef HAVE_CUDA
	if (CCV_STREAM_GET_CONTEXT(signal->type) == CCV_STREAM_CONTEXT_GPU)
		ccv_nnc_stream_compat_wait_signal(stream, signal);
#endif
}

void ccv_nnc_stream_signal_free(ccv_nnc_stream_signal_t* const signal)
{
#ifdef HAVE_CUDA
	if (CCV_STREAM_GET_CONTEXT(signal->type) == CCV_STREAM_CONTEXT_GPU)
		ccv_nnc_deinit_stream_signal(signal);
#endif
	ccfree(signal);
}

int ccv_nnc_device_count(const int type)
{
#ifdef HAVE_CUDA
	if (CCV_STREAM_GET_CONTEXT(type) == CCV_STREAM_CONTEXT_GPU)
		return ccv_nnc_gpu_device_count();
#endif
	return 1; // I don't get core count for CPU yet.
}

ccv_nnc_stream_scheduler_t* ccv_nnc_stream_context_get_scheduler(ccv_nnc_stream_context_t* const stream_context)
{
	ccv_nnc_stream_scheduler_t* scheduler = stream_context->scheduler;
	if (!scheduler)
		stream_context->scheduler = scheduler = (ccv_nnc_stream_scheduler_t*)cccalloc(1, sizeof(ccv_nnc_stream_scheduler_t));
	return scheduler;
}

void ccv_nnc_stream_scheduler_add_task(ccv_nnc_stream_scheduler_t* const scheduler, ccv_nnc_stream_task_t* const task)
{
	if (scheduler->tail)
	{
		scheduler->tail->next = task;
		task->prev = scheduler->tail;
	} else {
		scheduler->head = task;
		task->prev = 0;
	}
	scheduler->tail = task;
	task->next = 0;
}

static void _ccv_nnc_stream_scheduler_delete_task(ccv_nnc_stream_scheduler_t* const scheduler, ccv_nnc_stream_task_t* const task)
{
	if (task->prev)
		task->prev->next = task->next;
	else
		scheduler->head = task->next;
	if (task->next)
		task->next->prev = task->prev;
	else
		scheduler->tail = task->prev;
}

static void _ccv_nnc_stream_task_done(ccv_nnc_stream_task_t* const task)
{
	if (task->notify)
	{
		ccv_nnc_stream_task_t* const notify = task->notify;
		task->notify = 0;
		ccv_nnc_stream_scheduler_add_task(task->super, notify);
		int i;
		const int other_size = notify->other_size;
		notify->other_size = 0;
		ccv_nnc_stream_task_t* const* const others = notify->others;
		notify->others = 0;
		for (i = 0; i < other_size; i++)
			if (others[i] != task)
			{
				assert(others[i]->notify == notify);
				others[i]->notify = 0;
			}
	}
	ccv_nnc_stream_scheduler_t* const scheduler = task->super;
	if (!scheduler->empty_tasks)
		scheduler->empty_tasks = ccv_array_new(sizeof(ccv_nnc_stream_task_t*), 1, 0);
	ccv_array_push(scheduler->empty_tasks, &task);
}

// Second will invoke this blocking variant to schedule task on a newly created thread.
static void* _ccv_nnc_stream_schedule_main(void* userdata)
{
	ccv_nnc_stream_scheduler_t* const scheduler = (ccv_nnc_stream_scheduler_t*)userdata;
	for (;;)
	{
		pthread_mutex_lock(&scheduler->mutex);
		if (scheduler->head == 0 && scheduler->stream_wait_task_count == 0)
		{
			scheduler->active = 0;
			pthread_cond_broadcast(&scheduler->notify);
			pthread_mutex_unlock(&scheduler->mutex);
			break;
		}
		if (scheduler->head == 0)
		{
			pthread_cond_wait(&scheduler->wait, &scheduler->mutex);
			pthread_mutex_unlock(&scheduler->mutex);
		}
		ccv_nnc_stream_task_t* const task = scheduler->head;
		_ccv_nnc_stream_scheduler_delete_task(scheduler, task);
		pthread_mutex_unlock(&scheduler->mutex);
		swapcontext(&scheduler->caller, &task->context);
		task->context = scheduler->callee;
		if (task->done)
			_ccv_nnc_stream_task_done(task);
	}
	return 0;
}

// First will invoke this non-blocking variant to schedule task.
static void _ccv_nnc_stream_schedule_try(ccv_nnc_stream_scheduler_t* const scheduler)
{
	pthread_mutex_lock(&scheduler->mutex);
	if (scheduler->active)
	{
		pthread_mutex_unlock(&scheduler->mutex);
		return;
	}
	scheduler->active = 1;
	for (;;)
	{
		if (scheduler->head == 0 && scheduler->stream_wait_task_count == 0)
		{
			scheduler->active = 0;
			pthread_mutex_unlock(&scheduler->mutex);
			break;
		}
		if (scheduler->head == 0)
		{
			// Launch a thread to continue the execution.
			pthread_create(&scheduler->thread, 0, _ccv_nnc_stream_schedule_main, scheduler);
			pthread_mutex_unlock(&scheduler->mutex);
			break;
		}
		ccv_nnc_stream_task_t* const task = scheduler->head;
		_ccv_nnc_stream_scheduler_delete_task(scheduler, task);
		pthread_mutex_unlock(&scheduler->mutex);
		swapcontext(&scheduler->caller, &task->context);
		task->context = scheduler->callee;
		if (task->done)
			_ccv_nnc_stream_task_done(task);
		// Lock again for the next run loop.
		pthread_mutex_lock(&scheduler->mutex);
	}
}

void ccv_nnc_stream_schedule_task(ccv_nnc_stream_scheduler_t* const scheduler, ccv_nnc_stream_task_t* const task)
{
	int activate_scheduler = 0;
	pthread_mutex_lock(&scheduler->mutex);
	ccv_nnc_stream_scheduler_add_task(scheduler, task);
	if (!scheduler->active)
		activate_scheduler = 1;
	pthread_mutex_unlock(&scheduler->mutex);
	if (activate_scheduler)
		_ccv_nnc_stream_schedule_try(scheduler);
}

typedef union {
	void* ptr;
	uint32_t part[2];
} ccv_nnc_ptr_splitter_u;

static void _ccv_nnc_stream_task_entry_point(uint32_t part0, uint32_t part1)
{
	const ccv_nnc_ptr_splitter_u p = {
		.part = {
			part0, part1
		}
	};
	ccv_nnc_stream_task_t* const task = (ccv_nnc_stream_task_t*)p.ptr;
	task->func(task, task->userdata);
	ccv_nnc_stream_scheduler_t* const scheduler = task->super;
	task->done = 1;
	swapcontext(&scheduler->callee, &scheduler->caller);
}

ccv_nnc_stream_task_t* ccv_nnc_stream_task_new(ccv_nnc_stream_scheduler_t* const scheduler, const ccv_nnc_stream_task_f func, void* const userdata, const size_t userdata_size)
{
	ccv_nnc_stream_task_t* task;
	if (scheduler->empty_tasks && scheduler->empty_tasks->rnum)
	{
		task = *(ccv_nnc_stream_task_t**)ccv_array_get(scheduler->empty_tasks, scheduler->empty_tasks->rnum - 1);
		--scheduler->empty_tasks->rnum;
	} else {
		task = (ccv_nnc_stream_task_t*)cccalloc(1, sizeof(ccv_nnc_stream_task_t));
		task->stack = (char*)cccalloc(CCV_NNC_TASK_STACK_SIZE + userdata_size, 1);
		task->super = scheduler;
	}
	task->done = 0;
	task->func = func;
	if (userdata_size)
	{
		// If the size is available, we copy the userdata over.
		task->userdata = task->stack + CCV_NNC_TASK_STACK_SIZE;
		memcpy(task->userdata, userdata, userdata_size);
	} else
		task->userdata = userdata;
	getcontext(&task->context);
	task->context.uc_stack.ss_sp = task->stack;
	task->context.uc_stack.ss_size = CCV_NNC_TASK_STACK_SIZE;
	task->context.uc_link = 0;
	const ccv_nnc_ptr_splitter_u p = {
		.ptr = task,
	};
	makecontext(&task->context, (void (*)(void))_ccv_nnc_stream_task_entry_point, 2, p.part[0], p.part[1]);;
	return task;
}

void ccv_nnc_stream_task_resume(ccv_nnc_stream_task_t* const task)
{
	ccv_nnc_stream_scheduler_t* const scheduler = task->super;
	ucontext_t old_context = scheduler->caller;
	swapcontext(&scheduler->caller, &task->context);
	task->context = scheduler->callee;
	scheduler->caller = old_context;
	if (task->done)
		_ccv_nnc_stream_task_done(task);
}

void ccv_nnc_stream_task_synchronize(ccv_nnc_stream_task_t* const self, ccv_nnc_stream_context_t* const stream)
{
	if (!stream)
		return;
#ifdef HAVE_CUDA
	if (CCV_STREAM_GET_CONTEXT(stream->type) == CCV_STREAM_CONTEXT_GPU)
		ccv_nnc_stream_compat_task_synchronize(self, stream);
#endif
}

void ccv_nnc_stream_task_wait_any(ccv_nnc_stream_task_t* const self, ccv_nnc_stream_task_t* const* const others, const int other_size)
{
	self->other_size = other_size;
	self->others = others;
	int i;
	for (i = 0; i < other_size; i++)
	{
		assert(others[i]->notify == 0);
		others[i]->notify = self;
	}
	ccv_nnc_stream_scheduler_t* const scheduler = self->super;
	swapcontext(&scheduler->callee, &scheduler->caller);
}
