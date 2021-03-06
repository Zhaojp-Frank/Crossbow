#include "recorddataset.h"

#include "memorymanager.h"

#include "debug.h"
#include "utils.h"

static void *handle (void *args) {

	crossbowRecordDatasetP      self  = NULL;
	crossbowRecordDatasetEventP event = NULL;

	self = (crossbowRecordDatasetP) args;

	cpu_set_t set;
	int core = 1;
	CPU_ZERO (&set);
	CPU_SET  (core, &set);
	sched_setaffinity (0, sizeof(set), &set);
	info("Record dataset handler pinned on core %d\n", core);

	pthread_mutex_lock(&(self->lock));

	dbg("Record data set worker starts\n");

	while (! self->exit) {
		/* Block, waiting for an event */
		while (!self->exit && crossbowListSize(self->events) == 0) {
			pthread_cond_wait(&(self->cond), &(self->lock));
		}
		if (self->exit) {
			break;
		}

		/* Get task */
		event = (crossbowRecordDatasetEventP) crossbowListRemoveFirst (self->events);

		info("New task: fill %d\n", event->idx);

		/* Process event */

		crossbowRecordReaderReadProperly (self->reader,
				self->count,
				self->buffer->size,
				self->buffer->b,
				self->buffer->padding,
				self->buffer->theImages[event->idx],
				self->buffer->theLabels[event->idx],
				self->buffer->capacity
		);

		crossbowDoubleBufferUnlock (self->buffer, event->idx);

		/* Return task to free list */
		crossbowFree (event, sizeof(crossbow_record_dataset_event_t));
	}
	self->exited = 1;
	return self;
}

crossbowRecordDatasetP crossbowRecordDatasetCreate (int workers, int *capacity, int NB, int b, int *padding) {

	crossbowRecordDatasetP p = (crossbowRecordDatasetP) crossbowMalloc (sizeof(crossbow_record_dataset_t));

	p->reader = crossbowRecordReaderCreate (workers);

	p->buffer = crossbowDoubleBufferCreate (capacity, NB, b, padding);
	crossbowDoubleBufferRegister       (p->buffer);
	crossbowDoubleBufferAdviceWillNeed (p->buffer);

	/* Current data pointers */
	p->images = NULL;
	p->labels = NULL;

	/* Number of images and labels to decode/read per read call */
	p->count = (NB * b);

	/* Set-up worker thread */
	p->exit = 0;
	p->exited = 0;

	pthread_mutex_init (&(p->lock), NULL);
	pthread_cond_init  (&(p->cond), NULL);

	p->events = crossbowListCreate ();

	/* Launch thread */
	pthread_create (&(p->thread), NULL, handle, (void *) p);

	return p;
}

void crossbowRecordDatasetInitSafely (crossbowRecordDatasetP p) {

	nullPointerException (p);

	invalidConditionException (p->images == NULL);
	invalidConditionException (p->labels == NULL);

	info("%d items (%d/%d bytes), %d items per batch (%d/%d bytes padding), write to %p/%p, limited to %d/%d bytes\n",
			p->count,
			p->buffer->size[0],
			p->buffer->size[1],
			p->buffer->b,
			p->buffer->padding[0],
			p->buffer->padding[1],
			p->buffer->theImages[0],
			p->buffer->theLabels[0],
			p->buffer->capacity[0],
			p->buffer->capacity[1]);

	crossbowRecordReaderReadProperly (p->reader,
					p->count,
					p->buffer->size,
					p->buffer->b,
					p->buffer->padding,
					p->buffer->theImages[0],
					p->buffer->theLabels[0],
					p->buffer->capacity
			);

	/* Ensure that first call to `swap` will use the above buffer */
	p->buffer->idx = 1;
	crossbowDoubleBufferLock (p->buffer, 1);

	return;
}

void crossbowRecordDatasetSwap (crossbowRecordDatasetP p) {

	nullPointerException (p);

	/* Increment task index */
	int prev = p->buffer->idx;
	int next = (++p->buffer->idx) % 2;
	p->buffer->idx = next;


	info("Swap from %d to %d\n", prev, next);

	/* Unlock previous buffer */
	crossbowDoubleBufferUnlock (p->buffer, prev);

	/* Wait until at least one buffer is filled by reader */
	crossbowDoubleBufferLock (p->buffer, next);

	/* Change buffer pointers */
	p->images = p->buffer->theImages [next];
	p->labels = p->buffer->theLabels [next];

	/* Create a new task to fill previous buffer (but lock it first) */
	crossbowDoubleBufferLock (p->buffer, prev);

	crossbowRecordDatasetEventP event = crossbowMalloc (sizeof(crossbow_record_dataset_event_t));
	event->idx = prev;
	/* Schedule task */
	pthread_mutex_lock (&(p->lock));
	if (! p->exit) {
		crossbowListAppend (p->events, event);
		pthread_cond_signal (&(p->cond));
	}
	pthread_mutex_unlock(&(p->lock));

	return;
}

void crossbowRecordDatasetFree (crossbowRecordDatasetP p) {
	if (! p)
		return;

	if (p->buffer)
		crossbowDoubleBufferFree (p->buffer);

	if (p->reader)
		crossbowRecordReaderFree (p->reader);

	crossbowListFree (p->events);

	crossbowFree (p, sizeof(crossbow_record_dataset_t));
	return;
}
