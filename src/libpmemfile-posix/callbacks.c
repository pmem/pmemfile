/*
 * Copyright 2016-2017, Intel Corporation
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 *     * Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 *
 *     * Redistributions in binary form must reproduce the above copyright
 *       notice, this list of conditions and the following disclaimer in
 *       the documentation and/or other materials provided with the
 *       distribution.
 *
 *     * Neither the name of the copyright holder nor the names of its
 *       contributors may be used to endorse or promote products derived
 *       from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/*
 * callbacks.c -- transaction callback subsystem
 */

#include "callbacks.h"
#include "compiler_utils.h"
#include "internal.h"
#include "os_thread.h"
#include "out.h"
#include <errno.h>

struct tx_callback {
	cb_basic func;
	void *arg;
};

struct tx_callback_array {
	struct tx_callback *arr;

	/* Size of callbacks array. */
	unsigned size;

	/* Number of registered callbacks. */
	unsigned used;
};

struct all_callbacks {
	struct tx_callback_array forward;
	struct tx_callback_array backward;
};

static os_tls_key_t callbacks_key;

/*
 * cb_get -- returns current per-thread callback configuration
 */
static struct all_callbacks *
cb_get(void)
{
	struct all_callbacks *c = os_tls_get(callbacks_key);
	if (!c) {
		c = calloc(MAX_TX_STAGE, sizeof(struct all_callbacks));
		if (!c)
			return NULL;

		int ret = os_tls_set(callbacks_key, c);
		if (ret) {
			free(c);
			errno = ret;
			ERR("!os_tls_set");
			return NULL;
		}
	}

	return c;
}

/*
 * cb_check -- (internal) check whether current state
 * allows changing transaction callbacks
 */
static void
cb_check(const char *func)
{
	if (pmemobj_tx_stage() == TX_STAGE_NONE)
		FATAL("%s called outside of transaction", func);
}

/*
 * cb_append -- (internal) appends callback to specified array
 */
static int
cb_append(struct tx_callback_array *cb, cb_basic func, void *arg)
{
	if (cb->used == cb->size) {
		unsigned count = cb->size * 2;
		if (count == 0)
			count = 4;

		void *new_arr = realloc(cb->arr, count * sizeof(cb->arr[0]));
		if (!new_arr) {
			pmemfile_tx_abort(errno);
			return -1;
		}

		cb->arr = new_arr;
		cb->size = count;
	}

	unsigned num = cb->used++;
	cb->arr[num].func = func;
	cb->arr[num].arg = arg;

	return 0;
}

/*
 * cb_push_back -- registers transaction stage callback, to be called at the end
 */
int
cb_push_back(enum pobj_tx_stage stage, cb_basic func, void *arg)
{
	LOG(15, NULL);

	cb_check(__func__);
	struct all_callbacks *cbs = cb_get();
	if (!cbs)
		pmemobj_tx_abort(errno);

	return cb_append(&cbs[stage].forward, func, arg);
}

/*
 * cb_push_front -- registers transaction stage callback, to be called at
 * the beginning
 */
int
cb_push_front(enum pobj_tx_stage stage, cb_basic func, void *arg)
{
	LOG(15, NULL);

	cb_check(__func__);
	struct all_callbacks *cbs = cb_get();
	if (!cbs)
		pmemobj_tx_abort(errno);

	return cb_append(&cbs[stage].backward, func, arg);
}

/*
 * cb_free -- frees tx callbacks
 */
static void
cb_free(void *arg)
{
	LOG(15, NULL);
	struct all_callbacks *callbacks = arg;

	for (unsigned i = 0; i < MAX_TX_STAGE; ++i) {
		free(callbacks[i].forward.arr);
		callbacks[i].forward.arr = NULL;
		callbacks[i].forward.size = 0;
		callbacks[i].forward.used = 0;

		free(callbacks[i].backward.arr);
		callbacks[i].backward.arr = NULL;
		callbacks[i].backward.size = 0;
		callbacks[i].backward.used = 0;
	}

	free(callbacks);

	int ret = os_tls_set(callbacks_key, NULL);
	if (ret) {
		errno = ret;
		FATAL("!os_tls_set");
	}
}

/*
 * cb_init -- initializes callbacks subsystem
 */
void
cb_init(void)
{
	int ret = os_tls_key_create(&callbacks_key, cb_free);
	if (ret)
		FATAL("!os_tls_key_create");
}

/*
 * cb_fini -- cleans up state of tx callback module
 */
void
cb_fini(void)
{
	struct all_callbacks *c = os_tls_get(callbacks_key);
	if (c)
		cb_free(c);
}

/*
 * cb_queue -- basic implementation of transaction callback that calls multiple
 * functions
 */
void
cb_queue(PMEMobjpool *pop, enum pobj_tx_stage stage, void *arg)
{
	(void) pop;

	LOG(15, NULL);

	struct tx_callback_array *cb;
	unsigned num_callbacks;
	struct all_callbacks *file_callbacks = cb_get();
	if (!file_callbacks) {
		if (stage == TX_STAGE_WORK)
			pmemobj_tx_abort(errno);
		else
			/* not possible */
			FATAL("unable to allocate callbacks list");
	}

	cb = &file_callbacks[stage].backward;
	num_callbacks = cb->used;
	if (num_callbacks) {
		struct tx_callback *c = cb->arr;
		for (unsigned i = num_callbacks; i > 0; --i)
			c[i - 1].func(arg, c[i - 1].arg);
	}

	cb = &file_callbacks[stage].forward;
	num_callbacks = cb->used;
	if (num_callbacks) {
		struct tx_callback *c = cb->arr;
		for (unsigned i = 0; i < num_callbacks; ++i)
			c[i].func(arg, c[i].arg);
	}

	if (stage == TX_STAGE_NONE) {
		for (unsigned i = 0; i < MAX_TX_STAGE; ++i) {
			file_callbacks[i].backward.used = 0;
			file_callbacks[i].forward.used = 0;
		}
	}
}
