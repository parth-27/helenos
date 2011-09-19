/*
 * Copyright (c) 2011 Jiri Svoboda
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * - Redistributions of source code must retain the above copyright
 *   notice, this list of conditions and the following disclaimer.
 * - Redistributions in binary form must reproduce the above copyright
 *   notice, this list of conditions and the following disclaimer in the
 *   documentation and/or other materials provided with the distribution.
 * - The name of the author may not be used to endorse or promote products
 *   derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/** @addtogroup tcp
 * @{
 */

/**
 * @file
 */

#include <adt/list.h>
#include <errno.h>
#include <io/log.h>
#include <stdlib.h>
#include "iqueue.h"
#include "segment.h"
#include "seq_no.h"
#include "tcp_type.h"

void tcp_iqueue_init(tcp_iqueue_t *iqueue, tcp_conn_t *conn)
{
	list_initialize(&iqueue->list);
	iqueue->conn = conn;
}

void tcp_iqueue_insert_seg(tcp_iqueue_t *iqueue, tcp_segment_t *seg)
{
	tcp_iqueue_entry_t *iqe;
	log_msg(LVL_DEBUG, "tcp_iqueue_insert_seg()");

	iqe = calloc(1, sizeof(tcp_iqueue_entry_t));
	if (iqe == NULL) {
		log_msg(LVL_ERROR, "Failed allocating IQE.");
		return;
	}

	iqe->seg = seg;

	/* XXX Sort by sequence number */
	list_append(&iqe->link, &iqueue->list);
}

int tcp_iqueue_get_ready_seg(tcp_iqueue_t *iqueue, tcp_segment_t **seg)
{
	tcp_iqueue_entry_t *iqe;
	link_t *link;

	log_msg(LVL_DEBUG, "tcp_get_ready_seg()");

	link = list_first(&iqueue->list);
	if (link == NULL) {
		log_msg(LVL_DEBUG, "iqueue is empty");
		return ENOENT;
	}

	iqe = list_get_instance(link, tcp_iqueue_entry_t, link);

	while (!seq_no_segment_acceptable(iqueue->conn, iqe->seg)) {
		log_msg(LVL_DEBUG, "Skipping unacceptable segment");

		list_remove(&iqe->link);
		tcp_segment_delete(iqe->seg);

         	link = list_first(&iqueue->list);
		if (link == NULL) {
			log_msg(LVL_DEBUG, "iqueue is empty");
			return ENOENT;
		}

		iqe = list_get_instance(link, tcp_iqueue_entry_t, link);
	}

	/* Do not return segments that are not ready for processing */
	if (!seq_no_segment_ready(iqueue->conn, iqe->seg)) {
		log_msg(LVL_DEBUG, "Next segment not ready: SEG.SEQ=%u, "
		    "RCV.NXT=%u, SEG.LEN=%u", iqe->seg->seq,
		    iqueue->conn->rcv_nxt, iqe->seg->len);
		return ENOENT;
	}

	log_msg(LVL_DEBUG, "Returning ready segment %p", iqe->seg);
	list_remove(&iqe->link);
	*seg = iqe->seg;

	return EOK;
}

/**
 * @}
 */
