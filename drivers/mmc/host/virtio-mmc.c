#include "virtio-mmc.h"

struct virtmmc_data {
	struct virtio_device *vdev;
	struct mmc_host *mmc;
	struct virtqueue *vq;
	struct scatterlist sg;
};

static int virtio_mmc_probe(struct virtio_device *vdev) {
	printk(KERN_INFO "virtio_mmc_probe\n");
	return 0;
}

static void virtio_mmc_remove(struct virtio_device *vdev) {
	printk(KERN_INFO "virtio_mmc_remove\n");
}