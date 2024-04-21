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

struct mmc_req {
	u32 opcode;
	u32 arg;
};

struct virtio_mmc_request {
	u8 flags;

#define VIRTIO_MMC_REQUEST_DATA BIT(1)
#define VIRTIO_MMC_REQUEST_WRITE BIT(2)
#define VIRTIO_MMC_REQUEST_STOP BIT(3)

	struct mmc_req request;

	u8 buf[4096];
	size_t buf_len;

	struct mmc_req stop_req;
};

struct virtio_mmc_response {
	u32 cmd_resp[4];
	int cmd_resp_len;
	u8 buf[4096];
};

struct virtio_mmc_data {
	struct virtio_device *vdev;
	struct mmc_host *mmc;
	struct virtqueue *vq;
	struct mmc_request *current_request;

	struct virtio_mmc_request virtio_request;
	struct virtio_mmc_response virtio_response;
};

static void virtio_mmc_vq_callback(struct virtqueue *vq)
{
	unsigned int len;
	struct mmc_host *mmc;
	struct virtio_mmc_data *host;
	struct virtio_mmc_request *virtio_request;
	struct virtio_mmc_response *virtio_response;
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

	if (virtio_request->flags & VIRTIO_MMC_REQUEST_DATA) {
		if (!(virtio_request->flags & VIRTIO_MMC_REQUEST_WRITE)) {
			sg_copy_from_buffer(mrq->data->sg, mrq->data->sg_len, virtio_response->buf, virtio_request->buf_len);
		}
		mrq->data->bytes_xfered = virtio_request->buf_len;
	}

	host->current_request = NULL;
	mmc_request_done(mmc, mrq);
	complete(&request_handled);
}

static void virtio_mmc_send_request_to_qemu(struct virtio_mmc_data *data)
{
	struct scatterlist sg_out_linux, sg_in_linux;
	sg_init_one(&sg_out_linux, &data->virtio_request, sizeof(struct virtio_mmc_request));
	sg_init_one(&sg_in_linux, &data->virtio_response, sizeof(struct virtio_mmc_response));

	struct scatterlist *request[] = { &sg_out_linux, &sg_in_linux };

	if (virtqueue_add_sgs(data->vq, request, 1, 1, &data->virtio_response,
			      GFP_KERNEL) < 0) {
		printk(KERN_CRIT "virtqueue_add_sgs failed\n");
		return;
	}

	virtqueue_kick(data->vq);
	wait_for_completion(&request_handled);
}

static inline size_t __calculate_len(struct mmc_data *data)
{
	size_t len = 0;
	for (int i = 0; i < data->sg_len; i++)
		len += data->sg[i].length;
	return len;
}

/* MMC layer callbacks */

static void virtio_mmc_request(struct mmc_host *mmc, struct mmc_request *mrq)
{
	struct virtio_mmc_data *data;
	struct virtio_mmc_request *virtio_req;
	struct mmc_data *mrq_data;

	data = mmc_priv(mmc);
	BUG_ON(!data);
	data->current_request = mrq; // Saving the request for the callback

	virtio_req = &data->virtio_request;
	memset(virtio_req, 0, sizeof(struct virtio_mmc_request));

	BUG_ON(!mrq || !mrq->cmd);
	virtio_req->request.opcode = mrq->cmd->opcode;
	virtio_req->request.arg = mrq->cmd->arg;

	mrq_data = mrq->data;
	if (mrq_data) {
		virtio_req->flags |= VIRTIO_MMC_REQUEST_DATA;

		virtio_req->buf_len = __calculate_len(mrq->data);

		virtio_req->flags |= ((mrq_data->flags & MMC_DATA_WRITE) ? VIRTIO_MMC_REQUEST_WRITE : 0);
		if (virtio_req->flags & VIRTIO_MMC_REQUEST_WRITE) {
			sg_copy_to_buffer(mrq_data->sg, mrq_data->sg_len, virtio_req->buf, virtio_req->buf_len);
		}
	}

	if(mrq->stop) {
		virtio_req->flags |= VIRTIO_MMC_REQUEST_STOP;

		virtio_req->stop_req.opcode = mrq->stop->opcode;
		virtio_req->stop_req.arg = mrq->stop->arg;
	}

	virtio_mmc_send_request_to_qemu(data);
}

static void virtio_mmc_set_ios(struct mmc_host *mmc, struct mmc_ios *ios)
{
}

static int virtio_mmc_get_ro(struct mmc_host *mmc)
{
	return 0;
}

static int virtio_mmc_get_cd(struct mmc_host *mmc)
{
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