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
 * os_thread_windows.c -- wrappers around threading functions
 */

#include <errno.h>
#include <stdlib.h>
#include <windows.h>

#include "os_thread.h"
#include "out.h"

void
os_mutex_init(os_mutex_t *m)
{
	COMPILE_ERROR_ON(sizeof(os_mutex_t) < sizeof(CRITICAL_SECTION));

	InitializeCriticalSection((CRITICAL_SECTION *)m);
}

void
os_mutex_destroy(os_mutex_t *m)
{
	DeleteCriticalSection((CRITICAL_SECTION *)m);
}

void
os_mutex_lock(os_mutex_t *m)
{
	CRITICAL_SECTION *c = (CRITICAL_SECTION *)m;

	EnterCriticalSection(c);

	if (c->RecursionCount > 1) {
		LeaveCriticalSection(c);
		FATAL("double lock");
	}
}

void
os_mutex_unlock(os_mutex_t *m)
{
	LeaveCriticalSection((CRITICAL_SECTION *)m);
}

typedef struct {
	char is_write;
	SRWLOCK lock;
} win_rwlock_t;

void
os_rwlock_init(os_rwlock_t *m)
{
	COMPILE_ERROR_ON(sizeof(os_rwlock_t) < sizeof(win_rwlock_t));
	win_rwlock_t *rw = (win_rwlock_t *)m;
	InitializeSRWLock(&rw->lock);
}

void
os_rwlock_rdlock(os_rwlock_t *m)
{
	win_rwlock_t *rw = (win_rwlock_t *)m;
	AcquireSRWLockShared(&rw->lock);
	rw->is_write = 0;
}

void
os_rwlock_wrlock(os_rwlock_t *m)
{
	win_rwlock_t *rw = (win_rwlock_t *)m;
	AcquireSRWLockExclusive(&rw->lock);
	rw->is_write = 1;
}

void
os_rwlock_unlock(os_rwlock_t *m)
{
	win_rwlock_t *rw = (win_rwlock_t *)m;
	if (rw->is_write)
		ReleaseSRWLockExclusive(&rw->lock);
	else
		ReleaseSRWLockShared(&rw->lock);
}

void
os_rwlock_destroy(os_rwlock_t *m)
{
}

int
os_tls_key_create(os_tls_key_t *key, void (*destr_function)(void *))
{
	COMPILE_ERROR_ON(sizeof(os_tls_key_t) < sizeof(DWORD));

	/* XXX - destructor not supported */

	*key = TlsAlloc();
	if (*key == TLS_OUT_OF_INDEXES)
		return EAGAIN;
	if (!TlsSetValue(*key, NULL)) /* XXX - not needed? */
		return ENOMEM;
	return 0;
}

void *
os_tls_get(os_tls_key_t key)
{
	return TlsGetValue(key);
}

int
os_tls_set(os_tls_key_t key, const void *ptr)
{
	if (!TlsSetValue(key, (LPVOID)ptr))
		return ENOENT;
	return 0;
}

void
os_once(os_once_t *once, void (*init_routine)(void))
{
	COMPILE_ERROR_ON(sizeof(*once) < sizeof(long));

	/* XXX this implementation doesn't wait for init_routine to complete */
	if (!_InterlockedCompareExchange((long *)once, 1, 0))
		init_routine();
}
