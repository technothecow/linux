#include "virtio-mmc.h"
#include "asm-generic/int-ll64.h"
#include "linux/completion.h"
#include "linux/kern_levels.h"
#include "linux/mmc/core.h"
#include "linux/mmc/host.h"
#include "linux/printk.h"
#include "linux/scatterlist.h"
#include "linux/types.h"
#include "linux/virtio_config.h"
#include <linux/virtio.h>
#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/fs.h>

static DECLARE_COMPLETION(request_handled); // TODO: move it to virtio_mmc_data

typedef struct mmc_req {
	u32 opcode;
	u32 arg;
} mmc_req;

typedef struct virtio_mmc_req {
	mmc_req request;
	bool is_data;
	bool is_write;
	u8 buf[4096];
	size_t len;
	bool is_stop;
	mmc_req stop_req;
} virtio_mmc_req;

typedef struct virtio_mmc_resp {
	u32 cmd_resp[4];
	int cmd_resp_len;
	u8 buf[4096];
} virtio_mmc_resp;

typedef struct virtio_mmc_data {
	struct virtio_device *vdev;
	struct mmc_host *mmc;
	struct virtqueue *vq;
	struct mmc_request *current_request;

	virtio_mmc_req virtio_request;
	virtio_mmc_resp virtio_response;
} virtio_mmc_data;

static void virtio_mmc_vq_callback(struct virtqueue *vq)
{
	unsigned int len;
	struct mmc_host *mmc;
	virtio_mmc_data *host;
	virtio_mmc_req *virtio_request;
	virtio_mmc_resp *virtio_response;
	struct mmc_request *mrq;

	mmc = vq->vdev->priv;
	BUG_ON(!mmc);
	host = mmc_priv(mmc);
	BUG_ON(!host);
	mrq = host->current_request;
	BUG_ON(!mrq);
	virtio_request = &host->virtio_request;

	virtio_response = virtqueue_get_buf(vq, &len);
	BUG_ON(!virtio_response);

	memcpy(mrq->cmd->resp, virtio_response->cmd_resp, virtio_response->cmd_resp_len);

	if (virtio_request->is_data) {
		if (!virtio_request->is_write) {
			sg_copy_from_buffer(mrq->data->sg, mrq->data->sg_len, virtio_response->buf, virtio_request->len);
		}
		mrq->data->bytes_xfered = virtio_request->len;
	}

	host->current_request = NULL;
	mmc_request_done(mmc, mrq);
	complete(&request_handled);
}

static void virtio_mmc_send_request_to_qemu(virtio_mmc_data *data)
{
	struct scatterlist sg_out_linux, sg_in_linux;
	sg_init_one(&sg_out_linux, &data->virtio_request, sizeof(struct virtio_mmc_req));
	sg_init_one(&sg_in_linux, &data->virtio_response, sizeof(struct virtio_mmc_resp));

	struct scatterlist *request[] = { &sg_out_linux, &sg_in_linux };

	if (virtqueue_add_sgs(data->vq, request, 1, 1, &data->virtio_response,
			      GFP_KERNEL) < 0) {
		printk(KERN_CRIT "virtqueue_add_sgs failed\n");
		return;
	}

	virtqueue_kick(data->vq);
	wait_for_completion(&request_handled);
}

/* MMC layer callbacks */

static void virtio_mmc_request(struct mmc_host *mmc, struct mmc_request *mrq)
{
	virtio_mmc_data *data;
	virtio_mmc_req *virtio_req;

	data = mmc_priv(mmc);
	BUG_ON(!data);
	data->current_request = mrq; // Saving the request for the callback

	virtio_req = &data->virtio_request;

	BUG_ON(!mrq || !mrq->cmd);
	virtio_req->request.opcode = mrq->cmd->opcode;
	virtio_req->request.arg = mrq->cmd->arg;

	if (mrq->data) {
		virtio_req->is_data = true;

		size_t len = 0;
		for (int i = 0; i < mrq->data->sg_len; i++)
			len += mrq->data->sg[i].length;
		virtio_req->len = len;

		if (mrq->data->flags & MMC_DATA_WRITE) {
			data->virtio_request.is_write = true;
			sg_copy_to_buffer(mrq->data->sg, mrq->data->sg_len, &data->virtio_request.buf, len);
		} else {
			virtio_req->is_write = false;
		}
	} else {
		virtio_req->is_data = false;
	}

	if(mrq->stop) {
		virtio_req->is_stop = true;

		data->virtio_request.stop_req.opcode = mrq->stop->opcode;
		data->virtio_request.stop_req.arg = mrq->stop->arg;
	} else {
		data->virtio_request.is_stop = false;
	}

	virtio_mmc_send_request_to_qemu(data);
}

static void virtio_mmc_set_ios(struct mmc_host *mmc, struct mmc_ios *ios)
{
	printk(KERN_INFO "virtio_mmc_set_ios\n");
}

static int virtio_mmc_get_ro(struct mmc_host *mmc)
{
	printk(KERN_INFO "virtio_mmc_get_ro\n");
	return 0;
}

static int virtio_mmc_get_cd(struct mmc_host *mmc)
{
	printk(KERN_INFO "virtio_mmc_get_cd\n");
	return 1;
}

static const struct mmc_host_ops virtio_mmc_host_ops = {
	.request = virtio_mmc_request,
	.set_ios = virtio_mmc_set_ios,
	.get_ro = virtio_mmc_get_ro,
	.get_cd = virtio_mmc_get_cd,
};

static inline void __fill_host_attr(struct mmc_host *host)
{
	host->ops = &virtio_mmc_host_ops;
	host->f_min = 300000;
	host->f_max = 500000;
	host->ocr_avail = MMC_VDD_32_33 | MMC_VDD_33_34;
	host->caps = MMC_CAP_SD_HIGHSPEED;
	host->caps2 = MMC_CAP2_NO_SDIO | MMC_CAP2_NO_MMC | MMC_CAP2_HS400;
}

static int create_host(struct virtio_device *vdev)
{
	int err;
	struct mmc_host *host;
	struct virtio_mmc_data *data;

	host = mmc_alloc_host(sizeof(struct virtio_mmc_data), &vdev->dev);
	if(!host) {
		pr_err("virtio_mmc: Failed to allocate host\n");
		return -ENOMEM;
	}

	__fill_host_attr(host);

	vdev->priv = host;

	data = mmc_priv(host);
	data->vq = virtio_find_single_vq(vdev, virtio_mmc_vq_callback, "vq_name");
	if (!data->vq) {
		pr_err("virtio_mmc: Failed to find virtqueue\n");
		mmc_free_host(host);
		return -ENODEV;
	}

	err = mmc_add_host(host);
	if (err) {
		pr_err("virtio_mmc: Failed to add host\n");
		mmc_free_host(host);
		return err;
	}

	return 0;
}

static int virtio_mmc_probe(struct virtio_device *vdev)
{
	int err;

	init_completion(&request_handled);

	err = create_host(vdev);
	if (err)
		pr_err("virtio_mmc: Failed to make host\n");

	return 0;
}

static void remove_host(struct mmc_host *host)
{
	mmc_remove_host(host);
	mmc_free_host(host);
}

static void virtio_mmc_remove(struct virtio_device *vdev)
{
	struct mmc_host *host = vdev->priv;
	remove_host(host);
}