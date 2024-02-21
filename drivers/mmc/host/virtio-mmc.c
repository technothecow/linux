#include "virtio-mmc.h"
#include "linux/mmc/host.h"
#include <linux/virtio.h>
#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/fs.h>

struct virtmmc_data {
	struct virtio_device *vdev;
	struct mmc_host *mmc;
	struct virtqueue *vq;
	struct scatterlist sg;
	struct device *device;
	struct class *chardev_class;

	dev_t devt;
	struct cdev cdev;
};

static const struct file_operations virtio_mmc_dev_fops = {
	.owner = THIS_MODULE,
};

static void virtio_mmc_request(struct mmc_host *mmc, struct mmc_request *mrq) {
	printk(KERN_INFO "MMC Request details:\n");
	if(mrq->cmd) {
		printk(KERN_INFO "Command: %d\n", mrq->cmd->opcode);
		printk(KERN_INFO "Argument: %d\n", mrq->cmd->arg);
	} else {
		printk(KERN_INFO "Command: NULL\n");
	}
	if(mrq->data) {
		printk(KERN_INFO "Data blocks: %d\n", mrq->data->blocks);
		printk(KERN_INFO "Data block size: %d\n", mrq->data->blksz);
		printk(KERN_INFO "Data flags: %x\n", mrq->data->flags);
	} else {
		printk(KERN_INFO "Data: NULL\n");
	}
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

static int create_host(struct virtmmc_data *data) {
	int err;

	struct mmc_host *host = mmc_alloc_host(0, &data->vdev->dev);
	host->ops = &virtio_mmc_host_ops;
	host->f_min = 100000;
	host->f_max = 52000000;
	host->ocr_avail = MMC_VDD_32_33 | MMC_VDD_33_34;

	data->mmc = host;

	err = mmc_add_host(data->mmc);
	if (err) {
		printk(KERN_ERR "Failed to add host\n");
		return err;
	}

	

	return 0;
}

static void remove_host(struct mmc_host *host) {
	mmc_remove_host(host);
	mmc_free_host(host);
}

static int virtio_mmc_probe(struct virtio_device *vdev) {
	int err;
	printk(KERN_INFO "virtio_mmc_probe\n");

	struct virtmmc_data *data;

	data = kcalloc(1, sizeof(*data), GFP_KERNEL);
	if (!data) {
		printk(KERN_ERR "Failed to allocate memory for virtmmc_data\n");
		return -ENOMEM;
	}
	vdev->priv = data;
	data->vdev = vdev;
	printk(KERN_INFO "virtio_mmc_probe: data allocated\n");

	err = create_host(data);
	if(err) {
		printk(KERN_ERR "Failed to make host\n");
		goto free_data;
	}
	printk(KERN_INFO "virtio_mmc_probe: mmc host created\n");

	return 0;

// remove_host:
// 	remove_host(data->mmc);

free_data:
	kfree(data);

	return err;
}

static void virtio_mmc_remove(struct virtio_device *vdev) {
	printk(KERN_INFO "virtio_mmc_remove\n");

	struct virtmmc_data *data = vdev->priv;

	remove_host(data->mmc);

	kfree(data);
}