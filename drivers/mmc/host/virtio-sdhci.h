/* SPDX-License-Identifier: GPL-2.0-or-later
 *
 *  VirtIO SD/MMC driver
 *
 *  Author: Mikhail Krasheninnikov <krashmisha@gmail.com>
 */

#ifndef _VIRTIO_MMC_H
#define _VIRTIO_MMC_H

#include <linux/virtio.h>
#include <linux/virtio_ids.h>

static int virtio_sdhci_probe(struct virtio_device *vdev);

static void virtio_sdhci_remove(struct virtio_device *vdev);

static const struct virtio_device_id id_table[] = {
	{ VIRTIO_ID_SDHCI, VIRTIO_DEV_ANY_ID },
	{ 0 },
};

static struct virtio_driver virtio_sdhci_driver = {
	.driver = {
		.name	= KBUILD_MODNAME,
		.owner	= THIS_MODULE,
	},
	.id_table	= id_table,
	.probe		= virtio_sdhci_probe,
	.remove		= virtio_sdhci_remove,
};

module_virtio_driver(virtio_sdhci_driver);
MODULE_DEVICE_TABLE(virtio, id_table);

MODULE_AUTHOR("Mikhail Krasheninnikov");
MODULE_DESCRIPTION("VirtIO SD/MMC driver");
MODULE_LICENSE("GPL");

#endif
