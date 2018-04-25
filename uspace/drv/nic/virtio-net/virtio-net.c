/*
 * Copyright (c) 2018 Jakub Jermar
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

#include "virtio-net.h"

#include <stdio.h>
#include <stdint.h>

#include <ddf/driver.h>
#include <ddf/log.h>
#include <ops/nic.h>
#include <pci_dev_iface.h>

#include <nic.h>

#include <virtio-pci.h>

#define NAME	"virtio-net"

static errno_t virtio_net_initialize(ddf_dev_t *dev)
{
	nic_t *nic_data = nic_create_and_bind(dev);
	if (!nic_data)
		return ENOMEM;

	virtio_net_t *virtio_net = calloc(1, sizeof(virtio_net_t));
	if (!virtio_net) {
		nic_unbind_and_destroy(dev);
		return ENOMEM;
	}

	nic_set_specific(nic_data, virtio_net);

	errno_t rc = virtio_pci_dev_initialize(dev, &virtio_net->virtio_dev);
	if (rc != EOK)
		return rc;

	virtio_pci_common_cfg_t *cfg = virtio_net->virtio_dev.common_cfg;
	virtio_net_cfg_t *netcfg = virtio_net->virtio_dev.device_cfg;

	/*
	 * Perform device initialization as described in section 3.1.1 of the
	 * specification.
	 */

	/* 1. Reset the device */
	pio_write_8(&cfg->device_status, VIRTIO_DEV_STATUS_RESET);

	/* 2. Acknowledge we found the device */
	pio_write_8(&cfg->device_status, VIRTIO_DEV_STATUS_ACKNOWLEDGE);

	/* 3. We know how to drive the device */
	pio_write_8(&cfg->device_status, VIRTIO_DEV_STATUS_DRIVER);

	/* 4. Read the offered feature flags */
	pio_write_32(&cfg->device_feature_select, VIRTIO_NET_F_SELECT_PAGE_0);
	uint32_t features = pio_read_32(&cfg->device_feature);

	ddf_msg(LVL_NOTE, "offered features %x", features);
	features &= (1U << VIRTIO_NET_F_MAC);

	if (!features) {
		rc = ENOTSUP;
		goto fail;
	}

	/* 4. Write the accepted feature flags */
	pio_write_32(&cfg->driver_feature_select, VIRTIO_NET_F_SELECT_PAGE_0);
	pio_write_32(&cfg->driver_feature, features);

	/* 5. Set FEATURES_OK */
	pio_write_8(&cfg->device_status, VIRTIO_DEV_STATUS_FEATURES_OK);

	/* 6. Test if the device supports our feature subset */ 
	uint8_t status = pio_read_8(&cfg->device_status);
	if (!(status & VIRTIO_DEV_STATUS_FEATURES_OK)) {
		rc = ENOTSUP;
		goto fail;
	}

	/* 7. Perform device-specific setup */
	nic_address_t nic_addr;
	for (unsigned i = 0; i < 6; i++)
		nic_addr.address[i] = netcfg->mac[i];
	rc = nic_report_address(nic_data, &nic_addr);
	if (rc != EOK)
		goto fail;

	ddf_msg(LVL_NOTE, "MAC address: %02x:%02x:%02x:%02x:%02x:%02x",
	    nic_addr.address[0], nic_addr.address[1], nic_addr.address[2],
	    nic_addr.address[3], nic_addr.address[4], nic_addr.address[5]);

	/* 8. Go live */
	pio_write_8(&cfg->device_status, VIRTIO_DEV_STATUS_DRIVER_OK);

	return EOK;

fail:
	pio_write_8(&cfg->device_status, VIRTIO_DEV_STATUS_FAILED);
	virtio_pci_dev_cleanup(&virtio_net->virtio_dev);
	return rc;
}

static errno_t virtio_net_dev_add(ddf_dev_t *dev)
{
	ddf_msg(LVL_NOTE, "%s %s (handle = %zu)", __func__,
	    ddf_dev_get_name(dev), ddf_dev_get_handle(dev));

	errno_t rc = virtio_net_initialize(dev);
	if (rc != EOK)
		return rc;

	return ENOTSUP;
}

static ddf_dev_ops_t virtio_net_dev_ops;

static driver_ops_t virtio_net_driver_ops = {
	.dev_add = virtio_net_dev_add
};

static driver_t virtio_net_driver = {
	.name = NAME,
	.driver_ops = &virtio_net_driver_ops
};

static nic_iface_t virtio_net_nic_iface;

int main(void)
{
	printf("%s: HelenOS virtio-net driver\n", NAME);

	if (nic_driver_init(NAME) != EOK)
		return 1;

	nic_driver_implement(&virtio_net_driver_ops, &virtio_net_dev_ops,
	    &virtio_net_nic_iface);

	(void) ddf_log_init(NAME);
	return ddf_driver_main(&virtio_net_driver);
}
