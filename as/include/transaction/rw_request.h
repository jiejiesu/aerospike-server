/*
 * rw_request.h
 *
 * Copyright (C) 2016 Aerospike, Inc.
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

#pragma once

//==========================================================
// Includes.
//

#include <pthread.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "citrusleaf/alloc.h"
#include "citrusleaf/cf_atomic.h"
#include "citrusleaf/cf_clock.h"
#include "citrusleaf/cf_digest.h"

#include "dynbuf.h"
#include "msg.h"
#include "util.h"

#include "base/proto.h"
#include "base/rec_props.h"
#include "base/transaction.h"
#include "base/transaction_policy.h"
#include "fabric/partition.h"


//==========================================================
// Forward declarations.
//

struct rw_request_s;
struct iudf_origin_s;
struct as_batch_shared_s;


//==========================================================
// Typedefs.
//

typedef bool (*dup_res_done_cb) (struct rw_request_s* rw);
typedef void (*repl_write_done_cb) (struct rw_request_s* rw);
typedef void (*timeout_done_cb) (struct rw_request_s* rw);


typedef struct rw_wait_ele_s {
	as_transaction			tr; // TODO - only needs to be transaction head
	struct rw_wait_ele_s*	next;
} rw_wait_ele;


typedef struct rw_request_s {

	//------------------------------------------------------
	// Matches as_transaction.
	//

	cl_msg*				msgp;
	uint32_t			msg_fields;

	uint8_t				origin;
	uint8_t				from_flags;

	union {
		void*						any;
		as_file_handle*				proto_fd_h;
		cf_node						proxy_node;
		struct iudf_origin_s*		iudf_orig;
		struct as_batch_shared_s*	batch_shared;
	} from;

	union {
		uint32_t any;
		uint32_t batch_index;
		uint32_t proxy_tid;
	} from_data;

	cf_digest			keyd;

	cf_clock			start_time;
	cf_clock			benchmark_time;

	as_partition_reservation rsv;

	cf_clock			end_time;
	// Don't (yet) need result or flags.
	uint16_t			generation;
	uint32_t			void_time;
	// Don't (yet) need last_update_time.

	//
	// End of as_transaction look-alike.
	//------------------------------------------------------

	pthread_mutex_t		lock;

	rw_wait_ele*		wait_queue_head;

	bool				is_set_up; // TODO - redundant with timeout_cb
	bool				has_udf; // TODO - only for stats?
	bool				is_multiop;
	bool				respond_client_on_master_completion;

	// Store pickled data, for use in replica write.
	uint8_t*			pickled_buf;
	size_t				pickled_sz;
	as_rec_props		pickled_rec_props;

	// Store ops' responses here.
	cf_dyn_buf			response_db;

	// Manage responses of duplicate resolution and replica writes, or
	// alternatively, timeouts.
	uint32_t			tid;
	bool				dup_res_complete;
	dup_res_done_cb		dup_res_cb;
	repl_write_done_cb	repl_write_cb;
	timeout_done_cb		timeout_cb;

	// The request we're making, so we can retransmit if necessary. Will be the
	// duplicate request if we're in 'dup' phase, or the op (write) if we're in
	// the second phase but it's always the message we're sending out to the
	// nodes in the dest array.
	msg*				dest_msg;

	cf_clock			xmit_ms; // time of next retransmit
	uint32_t			retry_interval_ms; // interval to add for next retransmit

	// These three elements are used both for the duplicate resolution phase
	//  the "operation" (usually write) phase.
	int					n_dest_nodes;
	cf_node				dest_nodes[AS_CLUSTER_SZ];
	bool				dest_complete[AS_CLUSTER_SZ];

	// These elements are only used in the duplicate phase, and represent the
	// response that comes back from a given node
	msg*				dup_msg[AS_CLUSTER_SZ];
	int					dup_result_code[AS_CLUSTER_SZ];

} rw_request;


//==========================================================
// Public API.
//

rw_request* rw_request_create();
void rw_request_destroy(rw_request* rw);


static inline void
rw_request_hdestroy(void* pv)
{
	rw_request_destroy((rw_request*)pv);
}


static inline void
rw_request_release(rw_request* rw)
{
	if (0 == cf_rc_release(rw)) {
		rw_request_destroy(rw);
		cf_rc_free(rw);
	}
}


static inline uint32_t
rw_request_wait_q_depth(rw_request* rw)
{
	uint32_t depth = 0;
	rw_wait_ele* e = rw->wait_queue_head;

	while (e) {
		depth++;
		e = e->next;
	}

	return depth;
}
