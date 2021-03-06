/*
 * olock.c
 *
 * Copyright (C) 2008-2014 Aerospike, Inc.
 *
 * Portions may be licensed to Aerospike, Inc. under one or more contributor
 * license agreements.
 *
 * This program is free software: you can redistribute it and/or modify it under
 * the terms of the GNU Affero General Public License as published by the Free
 * Software Foundation, either version 3 of the License, or (at your option) any
 * later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 * FOR A PARTICULAR PURPOSE. See the GNU Affero General Public License for more
 * details.
 *
 * You should have received a copy of the GNU Affero General Public License
 * along with this program.  If not, see http://www.gnu.org/licenses/
 */

/*
 * The object lock system gives a list
 *
 */

#include "olock.h"

#include <errno.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#include <citrusleaf/cf_digest.h>
#include <citrusleaf/alloc.h>


// This ruins the notion that olocks are a generic class, but...
// (Perhaps better in index.c or record.c, if we ever make a record.h?)
olock *g_record_locks;


// an interesting detail: since this digest is used to choose among
// servers, you must use different bits to choose which OLOCK

//
// ASSUMES d is DIGEST and ol is OLOCK *
//

#define OLOCK_HASH(__ol, __d) ( ( (__d->digest[2] << 8) | (__d->digest[3]) ) & __ol->mask )

void
olock_lock(olock *ol, cf_digest *d)
{
	uint32_t n = OLOCK_HASH(ol, d);

	pthread_mutex_lock(&ol->locks[n]);
}

void
olock_vlock(olock *ol, cf_digest *d, pthread_mutex_t **vlock)
{
	uint32_t n = OLOCK_HASH(ol, d);

	*vlock = &ol->locks[n];

	if (0 != pthread_mutex_lock(*vlock)) {
		fprintf(stderr, "olock vlock failed\n");
	}
}

void
olock_unlock(olock *ol, cf_digest *d)
{
	uint32_t n = OLOCK_HASH(ol, d);

	if (0 != pthread_mutex_unlock(&ol->locks[n])) {
		fprintf(stderr, "olock unlock failed %d\n", errno);
	}
}

olock *
olock_create(uint32_t n_locks, bool mutex)
{
	olock *ol = cf_malloc(sizeof(olock) + (sizeof(pthread_mutex_t) * n_locks));

	if (! ol) {
		return 0;
	}

	uint32_t mask = n_locks - 1;

	if ((mask & n_locks) != 0) {
		fprintf(stderr, "olock: make sure your number of locks is a power of 2, n_locks aint\n");
		return 0;
	}

	ol->n_locks = n_locks;
	ol->mask = mask;

	for (int i = 0; i < n_locks; i++) {
		if (mutex) {
			pthread_mutex_init(&ol->locks[i], 0);
		}
		else {
			fprintf(stderr, "olock: todo add reader writer locks\n");
		}
	}

	return ol;
}

void
olock_destroy(olock *ol)
{
	for (int i = 0; i < ol->n_locks; i++) {
		pthread_mutex_destroy(&ol->locks[i]);
	}

	cf_free(ol);
}
