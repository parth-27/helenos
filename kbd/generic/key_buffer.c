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

#include <key_buffer.h>
#include <futex.h>

atomic_t keybuffer_futex = FUTEX_INITIALIZER;

/** Clear key buffer.
 */
void keybuffer_free(keybuffer_t *keybuffer) 
{
	futex_down(&keybuffer_futex);
	keybuffer->head = 0;
	keybuffer->tail = 0;
	keybuffer->items = 0;
	futex_up(&keybuffer_futex);
}

/** Key buffer initialization.
 *
 */
void keybuffer_init(keybuffer_t *keybuffer)
{
	keybuffer_free(keybuffer);
}

/** Get free space in buffer.
 * This function is useful for processing some scancodes that are translated 
 * to more than one character.
 * @return empty buffer space
 */
int keybuffer_available(keybuffer_t *keybuffer)
{
	return KEYBUFFER_SIZE - keybuffer->items;
}

/**
 * @return nonzero, if buffer is not empty.
 */
int keybuffer_empty(keybuffer_t *keybuffer)
{
	return (keybuffer->items == 0);
}

/** Push key to key buffer.
 * If buffer is full, character is ignored.
 * @param key code of stored key
 */
void keybuffer_push(keybuffer_t *keybuffer, int key)
{
	futex_down(&keybuffer_futex);
	if (keybuffer->items < KEYBUFFER_SIZE) {
		keybuffer->fifo[keybuffer->tail] = key;
		keybuffer->tail = (keybuffer->tail + 1) % KEYBUFFER_SIZE;
		keybuffer->items++;
	}
	futex_up(&keybuffer_futex);
}

/** Pop character from buffer.
 * @param c pointer to space where to store character from buffer.
 * @return zero on empty buffer, nonzero else
 */
int keybuffer_pop(keybuffer_t *keybuffer, int *c)
{
	futex_down(&keybuffer_futex);
	if (keybuffer->items > 0) {
		keybuffer->items--;
		*c = (keybuffer->fifo[keybuffer->head]) ;
		keybuffer->head = (keybuffer->head + 1) % KEYBUFFER_SIZE;
		futex_up(&keybuffer_futex);
		return 1;
	}
	futex_up(&keybuffer_futex);
	return 0;
}


