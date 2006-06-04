/*
 * Copyright (C) 2006 Josef Cejka
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

#include <ipc/ipc.h>
#include <ipc/services.h>
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <ipc/ns.h>
#include <errno.h>
#include <arch/kbd.h>
#include <kbd.h>
#include <libadt/fifo.h>
#include <key_buffer.h>

#define NAME "KBD"

int main(int argc, char **argv)
{
	ipc_call_t call;
	ipc_callid_t callid;
	int res;
	ipcarg_t phonead;
	ipcarg_t phoneid;
	char connected = 0;
	keybuffer_t keybuffer;	
	ipcarg_t retval, arg1, arg2;
	
	//open("null",0);
	//open("stdout",0);
	
	/* Initialize arch dependent parts */
	if (!(res = kbd_arch_init())) {
			return -1;
			};
	
	/* Initialize key buffer */
	keybuffer_init(&keybuffer);
	
	/* Register service at nameserver */
	
	if ((res = ipc_connect_to_me(PHONE_NS, SERVICE_KEYBOARD, 0, &phonead)) != 0) {
		return -1;
	};
	while (1) {
		callid = ipc_wait_for_call(&call);
		switch (IPC_GET_METHOD(call)) {
			case IPC_M_PHONE_HUNGUP:
				connected = 0;
				retval = 0;
				break;
			case IPC_M_CONNECT_ME_TO:
				/* Only one connected client allowed */
				if (connected) {
					retval = ELIMIT;
				} else {
					retval = 0;
					connected = 1;
				}
				break;
			case IPC_M_CONNECT_TO_ME:
				phoneid = IPC_GET_ARG3(call);
				retval = 0;
				break;

			case IPC_M_INTERRUPT:
				if (connected) {
					int chr;
					/* recode to ASCII - one interrupt can produce more than one code so result is stored in fifo */
					kbd_arch_process(&keybuffer, IPC_GET_ARG2(call));

					retval = 0;
					

					while (!keybuffer_empty(&keybuffer)) {
						if (!keybuffer_pop(&keybuffer, (int *)&chr)) {
							break;
						}
						{
							arg1=chr;
							send_call(phoneid, KBD_PUSHCHAR, arg1);
						}    
					}

				}
				break;
			default:
				retval = ENOENT;
				break;
		}

		if (! (callid & IPC_CALLID_NOTIFICATION)) {
			ipc_answer_fast(callid, retval, arg1, arg2);
		}
	}
}

