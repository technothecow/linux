#include "virtio-mmc.h"
#include "linux/kern_levels.h"
#include "linux/mmc/host.h"
#include "linux/scatterlist.h"
#include "linux/virtio_config.h"
#include <linux/virtio.h>
#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/fs.h>

struct virtio_mmc_req {
	u32 opcode;
	u32 arg;
	u32 flags;
	u32 blocks;
	u32 blksz;
};

struct virtio_mmc_data {
	struct virtio_device *vdev;
	struct mmc_host *mmc;
	struct virtqueue *vq;
	struct scatterlist sg;
	struct virtio_mmc_req req;

	dev_t devt;
	struct cdev cdev;
};

static void virtio_mmc_request(struct mmc_host *mmc, struct mmc_request *mrq) {
	struct virtio_mmc_data *data = mmc_priv(mmc);
	if(!data) {
		printk(KERN_CRIT "request: No data in mmc_priv\n");
		return;
	}

	printk(KERN_INFO "MMC Request details:\n");
	if(mrq->cmd) {
		printk(KERN_INFO "Command: %d\n", mrq->cmd->opcode);
		printk(KERN_INFO "Argument: %d\n", mrq->cmd->arg);
		data->req.opcode = mrq->cmd->opcode;
		data->req.arg = mrq->cmd->arg;
	} else {
		printk(KERN_INFO "Command: NULL\n");
	}
	if(mrq->data) {
		printk(KERN_INFO "Data blocks: %d\n", mrq->data->blocks);
		printk(KERN_INFO "Data block size: %d\n", mrq->data->blksz);
		printk(KERN_INFO "Data flags: %x\n", mrq->data->flags);
		data->req.blocks = mrq->data->blocks;
		data->req.blksz = mrq->data->blksz;
		data->req.flags = mrq->data->flags;
	} else {
		printk(KERN_INFO "Data: NULL\n");
	}

	struct scatterlist sg_in_qemu;
	sg_init_one(&sg_in_qemu, &data->req, sizeof(struct virtio_mmc_req));

	struct scatterlist *request[] = {&sg_in_qemu};

	if (virtqueue_add_sgs(data->vq, request, 1, 0, &sg_in_qemu, GFP_KERNEL) < 0) {
		printk(KERN_CRIT "virtqueue_add_sgs failed\n");
		return;
	}

	printk(KERN_INFO "virtqueue_kick\n");
	virtqueue_kick(data->vq);
}

static void virtio_mmc_set_ios(struct mmc_host *mmc, struct mmc_ios *ios) {
	printk(KERN_INFO "virtio_mmc_set_ios\n");
}

static int virtio_mmc_get_ro(struct mmc_host *mmc) {
	printk(KERN_INFO "virtio_mmc_get_ro\n");
	return 0;
}

static int virtio_mmc_get_cd(struct mmc_host *mmc) {
	printk(KERN_INFO "virtio_mmc_get_cd\n");
	return 1;
}

static void virtio_mmc_enable_sdio_irq(struct mmc_host *mmc, int enable) {
	printk(KERN_INFO "virtio_mmc_enable_sdio_irq, enable = %d", enable);
}

static int virtio_mmc_start_signal_voltage_switch(struct mmc_host *mmc, struct mmc_ios *ios) {
	printk(KERN_INFO "virtio_mmc_start_signal_voltage_switch\n");
	return 0;
}

static const struct mmc_host_ops virtio_mmc_host_ops = {
	.request = virtio_mmc_request,
	.set_ios = virtio_mmc_set_ios,
	.get_ro = virtio_mmc_get_ro,
	.get_cd = virtio_mmc_get_cd,
	.enable_sdio_irq = virtio_mmc_enable_sdio_irq,
	.start_signal_voltage_switch = virtio_mmc_start_signal_voltage_switch,
};

static int create_host(struct virtio_device *vdev) {
	int err;

	struct mmc_host *host = mmc_alloc_host(sizeof(struct virtio_mmc_data), &vdev->dev);
	host->ops = &virtio_mmc_host_ops;
	host->f_min = 100000;
	host->f_max = 52000000;
	host->ocr_avail = MMC_VDD_32_33 | MMC_VDD_33_34;

	err = mmc_add_host(host);
	if (err) {
		printk(KERN_ERR "Failed to add host\n");
		mmc_free_host(host);
		return err;
	}

	vdev->priv = host;

	return 0;
}

static void remove_host(struct mmc_host *host) {
	mmc_remove_host(host);
	mmc_free_host(host);
}

static void virtio_mmc_vq_callback(struct virtqueue *vq) {
	printk(KERN_INFO "virtio_mmc_vq_callback\n");
}

static int virtio_mmc_probe(struct virtio_device *vdev) {
	int err;
	printk(KERN_INFO "virtio_mmc_probe\n");

	err = create_host(vdev);
	if(err) {
		printk(KERN_ERR "Failed to make host\n");
	}
	printk(KERN_INFO "virtio_mmc_probe: mmc host created\n");

	struct virtio_mmc_data *data = mmc_priv(vdev->priv);

	data->vq = virtio_find_single_vq(vdev, virtio_mmc_vq_callback, "vq_name");
	if (!data->vq) {
		printk(KERN_ERR "Failed to find virtqueue\n");
		err = -ENODEV;
		goto remove_host;
	}

	return 0;

remove_host:
	remove_host(data->mmc);

	return err;
}

static void virtio_mmc_remove(struct virtio_device *vdev) {
	printk(KERN_INFO "virtio_mmc_remove\n");

	struct mmc_host *host = vdev->priv;
	remove_host(host);
}