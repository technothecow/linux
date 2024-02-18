#ifndef _VIRTIO_MMC_H
#define _VIRTIO_MMC_H

#include <linux/virtio.h>
#include <linux/virtio_ids.h>

#define VIRTIO_MMC_DEV_ID 42
#define VIRTIO_MMC_DEV_NAME "mmcblk"
#define VIRTIO_MMC_FIRST_MINOR 0
#define VIRTIO_MMC_MINOR_COUNT 1

static int virtio_mmc_probe(struct virtio_device *vdev);

static void virtio_mmc_remove(struct virtio_device *vdev);

static const struct virtio_device_id id_table[] = {
	{ VIRTIO_MMC_DEV_ID, VIRTIO_DEV_ANY_ID },
	{ 0 },
};

static struct virtio_driver virtio_mmc_driver = {
    .driver = {
        .name = KBUILD_MODNAME,
        .owner = THIS_MODULE,
    },
    .id_table = id_table,
    .probe = virtio_mmc_probe,
    .remove = virtio_mmc_remove,
};

module_virtio_driver(virtio_mmc_driver);
MODULE_DEVICE_TABLE(virtio, id_table);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("VirtIO MMC driver");
MODULE_AUTHOR("mi");

#endif