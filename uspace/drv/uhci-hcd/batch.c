/*
 * Copyright (c) 2011 Jan Vesely
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
/** @addtogroup drvusbuhcihc
 * @{
 */
/** @file
 * @brief UHCI driver USB transfer structure
 */
#include <errno.h>
#include <str_error.h>

#include <usb/usb.h>
#include <usb/debug.h>

#include "batch.h"
#include "transfer_list.h"
#include "hw_struct/transfer_descriptor.h"
#include "utils/malloc32.h"

#define DEFAULT_ERROR_COUNT 3

typedef struct uhci_batch {
	qh_t *qh;
	td_t *tds;
	size_t td_count;
} uhci_batch_t;

static void batch_control(usb_transfer_batch_t *instance,
    usb_packet_id data_stage, usb_packet_id status_stage);
static void batch_data(usb_transfer_batch_t *instance, usb_packet_id pid);
static void batch_call_in_and_dispose(usb_transfer_batch_t *instance);
static void batch_call_out_and_dispose(usb_transfer_batch_t *instance);


/** Allocate memory and initialize internal data structure.
 *
 * @param[in] fun DDF function to pass to callback.
 * @param[in] ep Communication target
 * @param[in] buffer Data source/destination.
 * @param[in] size Size of the buffer.
 * @param[in] setup_buffer Setup data source (if not NULL)
 * @param[in] setup_size Size of setup_buffer (should be always 8)
 * @param[in] func_in function to call on inbound transfer completion
 * @param[in] func_out function to call on outbound transfer completion
 * @param[in] arg additional parameter to func_in or func_out
 * @return Valid pointer if all substructures were successfully created,
 * NULL otherwise.
 *
 * Determines the number of needed transfer descriptors (TDs).
 * Prepares a transport buffer (that is accessible by the hardware).
 * Initializes parameters needed for the transfer and callback.
 */
usb_transfer_batch_t * batch_get(ddf_fun_t *fun, endpoint_t *ep,
    char *buffer, size_t buffer_size, char* setup_buffer, size_t setup_size,
    usbhc_iface_transfer_in_callback_t func_in,
    usbhc_iface_transfer_out_callback_t func_out, void *arg)
{
	assert(ep);
	assert(func_in == NULL || func_out == NULL);
	assert(func_in != NULL || func_out != NULL);

#define CHECK_NULL_DISPOSE_RETURN(ptr, message...) \
	if (ptr == NULL) { \
		usb_log_error(message); \
		if (instance) { \
			batch_dispose(instance); \
		} \
		return NULL; \
	} else (void)0

	usb_transfer_batch_t *instance = malloc(sizeof(usb_transfer_batch_t));
	CHECK_NULL_DISPOSE_RETURN(instance,
	    "Failed to allocate batch instance.\n");
	usb_target_t target =
	    { .address = ep->address, .endpoint = ep->endpoint };
	usb_transfer_batch_init(instance, ep,
	    buffer, NULL, buffer_size, NULL, setup_size,
	    func_in, func_out, arg, fun, NULL);


	uhci_batch_t *data = malloc(sizeof(uhci_batch_t));
	CHECK_NULL_DISPOSE_RETURN(data, "Failed to allocate batch data.\n");
	bzero(data, sizeof(uhci_batch_t));
	instance->private_data = data;

	data->td_count =
	    (buffer_size + ep->max_packet_size - 1) / ep->max_packet_size;
	if (ep->transfer_type == USB_TRANSFER_CONTROL) {
		data->td_count += 2;
	}

	data->tds = malloc32(sizeof(td_t) * data->td_count);
	CHECK_NULL_DISPOSE_RETURN(
	    data->tds, "Failed to allocate transfer descriptors.\n");
	bzero(data->tds, sizeof(td_t) * data->td_count);

	data->qh = malloc32(sizeof(qh_t));
	CHECK_NULL_DISPOSE_RETURN(data->qh,
	    "Failed to allocate batch queue head.\n");
	qh_init(data->qh);
	qh_set_element_td(data->qh, addr_to_phys(data->tds));

	if (buffer_size > 0) {
		instance->data_buffer = malloc32(buffer_size);
		CHECK_NULL_DISPOSE_RETURN(instance->data_buffer,
		    "Failed to allocate device accessible buffer.\n");
	}

	if (setup_size > 0) {
		instance->setup_buffer = malloc32(setup_size);
		CHECK_NULL_DISPOSE_RETURN(instance->setup_buffer,
		    "Failed to allocate device accessible setup buffer.\n");
		memcpy(instance->setup_buffer, setup_buffer, setup_size);
	}

	usb_log_debug("Batch(%p) %d:%d memory structures ready.\n",
	    instance, target.address, target.endpoint);
	return instance;
}
/*----------------------------------------------------------------------------*/
/** Check batch TDs for activity.
 *
 * @param[in] instance Batch structure to use.
 * @return False, if there is an active TD, true otherwise.
 *
 * Walk all TDs. Stop with false if there is an active one (it is to be
 * processed). Stop with true if an error is found. Return true if the last TD
 * is reached.
 */
bool batch_is_complete(usb_transfer_batch_t *instance)
{
	assert(instance);
	uhci_batch_t *data = instance->private_data;
	assert(data);

	usb_log_debug2("Batch(%p) checking %d transfer(s) for completion.\n",
	    instance, data->td_count);
	instance->transfered_size = 0;
	size_t i = 0;
	for (;i < data->td_count; ++i) {
		if (td_is_active(&data->tds[i])) {
			return false;
		}

		instance->error = td_status(&data->tds[i]);
		if (instance->error != EOK) {
			usb_log_debug("Batch(%p) found error TD(%d):%x.\n",
			    instance, i, data->tds[i].status);
			td_print_status(&data->tds[i]);

			assert(instance->ep != NULL);
			endpoint_toggle_set(instance->ep,
			    td_toggle(&data->tds[i]));
			if (i > 0)
				goto substract_ret;
			return true;
		}

		instance->transfered_size += td_act_size(&data->tds[i]);
		if (td_is_short(&data->tds[i]))
			goto substract_ret;
	}
substract_ret:
	instance->transfered_size -= instance->setup_size;
	return true;
}
/*----------------------------------------------------------------------------*/
/** Prepares control write transfer.
 *
 * @param[in] instance Batch structure to use.
 *
 * Uses generic control function with pids OUT and IN.
 */
void batch_control_write(usb_transfer_batch_t *instance)
{
	assert(instance);
	/* We are data out, we are supposed to provide data */
	memcpy(instance->data_buffer, instance->buffer, instance->buffer_size);
	batch_control(instance, USB_PID_OUT, USB_PID_IN);
	instance->next_step = batch_call_out_and_dispose;
	usb_log_debug("Batch(%p) CONTROL WRITE initialized.\n", instance);
}
/*----------------------------------------------------------------------------*/
/** Prepares control read transfer.
 *
 * @param[in] instance Batch structure to use.
 *
 * Uses generic control with pids IN and OUT.
 */
void batch_control_read(usb_transfer_batch_t *instance)
{
	assert(instance);
	batch_control(instance, USB_PID_IN, USB_PID_OUT);
	instance->next_step = batch_call_in_and_dispose;
	usb_log_debug("Batch(%p) CONTROL READ initialized.\n", instance);
}
/*----------------------------------------------------------------------------*/
/** Prepare interrupt in transfer.
 *
 * @param[in] instance Batch structure to use.
 *
 * Data transfer with PID_IN.
 */
void batch_interrupt_in(usb_transfer_batch_t *instance)
{
	assert(instance);
	batch_data(instance, USB_PID_IN);
	instance->next_step = batch_call_in_and_dispose;
	usb_log_debug("Batch(%p) INTERRUPT IN initialized.\n", instance);
}
/*----------------------------------------------------------------------------*/
/** Prepare interrupt out transfer.
 *
 * @param[in] instance Batch structure to use.
 *
 * Data transfer with PID_OUT.
 */
void batch_interrupt_out(usb_transfer_batch_t *instance)
{
	assert(instance);
	/* We are data out, we are supposed to provide data */
	memcpy(instance->data_buffer, instance->buffer, instance->buffer_size);
	batch_data(instance, USB_PID_OUT);
	instance->next_step = batch_call_out_and_dispose;
	usb_log_debug("Batch(%p) INTERRUPT OUT initialized.\n", instance);
}
/*----------------------------------------------------------------------------*/
/** Prepare bulk in transfer.
 *
 * @param[in] instance Batch structure to use.
 *
 * Data transfer with PID_IN.
 */
void batch_bulk_in(usb_transfer_batch_t *instance)
{
	assert(instance);
	batch_data(instance, USB_PID_IN);
	instance->next_step = batch_call_in_and_dispose;
	usb_log_debug("Batch(%p) BULK IN initialized.\n", instance);
}
/*----------------------------------------------------------------------------*/
/** Prepare bulk out transfer.
 *
 * @param[in] instance Batch structure to use.
 *
 * Data transfer with PID_OUT.
 */
void batch_bulk_out(usb_transfer_batch_t *instance)
{
	assert(instance);
	/* We are data out, we are supposed to provide data */
	memcpy(instance->data_buffer, instance->buffer, instance->buffer_size);
	batch_data(instance, USB_PID_OUT);
	instance->next_step = batch_call_out_and_dispose;
	usb_log_debug("Batch(%p) BULK OUT initialized.\n", instance);
}
/*----------------------------------------------------------------------------*/
/** Prepare generic data transfer
 *
 * @param[in] instance Batch structure to use.
 * @param[in] pid Pid to use for data transactions.
 *
 * Transactions with alternating toggle bit and supplied pid value.
 * The last transfer is marked with IOC flag.
 */
void batch_data(usb_transfer_batch_t *instance, usb_packet_id pid)
{
	assert(instance);
	uhci_batch_t *data = instance->private_data;
	assert(data);

	const bool low_speed = instance->ep->speed == USB_SPEED_LOW;
	int toggle = endpoint_toggle_get(instance->ep);
	assert(toggle == 0 || toggle == 1);

	size_t td = 0;
	size_t remain_size = instance->buffer_size;
	char *buffer = instance->data_buffer;
	while (remain_size > 0) {
		const size_t packet_size =
		    (instance->ep->max_packet_size > remain_size) ?
		    remain_size : instance->ep->max_packet_size;

		td_t *next_td = (td + 1 < data->td_count)
		    ? &data->tds[td + 1] : NULL;


		usb_target_t target =
		    { instance->ep->address, instance->ep->endpoint };

		assert(td < data->td_count);
		td_init(
		    &data->tds[td], DEFAULT_ERROR_COUNT, packet_size,
		    toggle, false, low_speed, target, pid, buffer, next_td);

		++td;
		toggle = 1 - toggle;
		buffer += packet_size;
		assert(packet_size <= remain_size);
		remain_size -= packet_size;
	}
	td_set_ioc(&data->tds[td - 1]);
	endpoint_toggle_set(instance->ep, toggle);
}
/*----------------------------------------------------------------------------*/
/** Prepare generic control transfer
 *
 * @param[in] instance Batch structure to use.
 * @param[in] data_stage Pid to use for data tds.
 * @param[in] status_stage Pid to use for data tds.
 *
 * Setup stage with toggle 0 and USB_PID_SETUP.
 * Data stage with alternating toggle and pid supplied by parameter.
 * Status stage with toggle 1 and pid supplied by parameter.
 * The last transfer is marked with IOC.
 */
void batch_control(usb_transfer_batch_t *instance,
   usb_packet_id data_stage, usb_packet_id status_stage)
{
	assert(instance);
	uhci_batch_t *data = instance->private_data;
	assert(data);
	assert(data->td_count >= 2);

	const bool low_speed = instance->ep->speed == USB_SPEED_LOW;
	const usb_target_t target =
	    { instance->ep->address, instance->ep->endpoint };

	/* setup stage */
	td_init(
	    data->tds, DEFAULT_ERROR_COUNT, instance->setup_size, 0, false,
	    low_speed, target, USB_PID_SETUP, instance->setup_buffer,
	    &data->tds[1]);

	/* data stage */
	size_t td = 1;
	unsigned toggle = 1;
	size_t remain_size = instance->buffer_size;
	char *buffer = instance->data_buffer;
	while (remain_size > 0) {
		const size_t packet_size =
		    (instance->ep->max_packet_size > remain_size) ?
		    remain_size : instance->ep->max_packet_size;

		td_init(
		    &data->tds[td], DEFAULT_ERROR_COUNT, packet_size,
		    toggle, false, low_speed, target, data_stage,
		    buffer, &data->tds[td + 1]);

		++td;
		toggle = 1 - toggle;
		buffer += packet_size;
		assert(td < data->td_count);
		assert(packet_size <= remain_size);
		remain_size -= packet_size;
	}

	/* status stage */
	assert(td == data->td_count - 1);

	td_init(
	    &data->tds[td], DEFAULT_ERROR_COUNT, 0, 1, false, low_speed,
	    target, status_stage, NULL, NULL);
	td_set_ioc(&data->tds[td]);

	usb_log_debug2("Control last TD status: %x.\n",
	    data->tds[td].status);
}
/*----------------------------------------------------------------------------*/
qh_t * batch_qh(usb_transfer_batch_t *instance)
{
	assert(instance);
	uhci_batch_t *data = instance->private_data;
	assert(data);
	return data->qh;
}
/*----------------------------------------------------------------------------*/
/** Helper function, calls callback and correctly destroys batch structure.
 *
 * @param[in] instance Batch structure to use.
 */
void batch_call_in_and_dispose(usb_transfer_batch_t *instance)
{
	assert(instance);
	usb_transfer_batch_call_in(instance);
	batch_dispose(instance);
}
/*----------------------------------------------------------------------------*/
/** Helper function calls callback and correctly destroys batch structure.
 *
 * @param[in] instance Batch structure to use.
 */
void batch_call_out_and_dispose(usb_transfer_batch_t *instance)
{
	assert(instance);
	usb_transfer_batch_call_out(instance);
	batch_dispose(instance);
}
/*----------------------------------------------------------------------------*/
/** Correctly dispose all used data structures.
 *
 * @param[in] instance Batch structure to use.
 */
void batch_dispose(usb_transfer_batch_t *instance)
{
	assert(instance);
	uhci_batch_t *data = instance->private_data;
	assert(data);
	usb_log_debug("Batch(%p) disposing.\n", instance);
	/* free32 is NULL safe */
	free32(data->tds);
	free32(data->qh);
	free32(instance->setup_buffer);
	free32(instance->data_buffer);
	free(data);
	free(instance);
}
/**
 * @}
 */
