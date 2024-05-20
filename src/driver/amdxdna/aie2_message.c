// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2023-2024, Advanced Micro Devices, Inc.
 */

#include <linux/kthread.h>
#include <drm/drm_cache.h>

#include "drm_local/amdxdna_accel.h"
#include "amdxdna_mailbox_helper.h"
#include "amdxdna_ctx.h"
#include "aie2_msg_priv.h"
#include "aie2_pci.h"

#define DECLARE_AIE2_MSG(name, op) \
	DECLARE_XDNA_MSG_COMMON(name, op, MAX_AIE2_STATUS_CODE)

static int aie2_send_mgmt_msg_wait(struct amdxdna_dev_hdl *ndev,
				   struct xdna_mailbox_msg *msg)
{
	struct amdxdna_dev *xdna = ndev->xdna;
	struct xdna_notify *hdl = msg->handle;
	int ret;

	if (!ndev->mgmt_chann)
		return -ENODEV;

	drm_WARN_ON(&xdna->ddev, !mutex_is_locked(&xdna->dev_lock));
	ret = xdna_send_msg_wait(xdna, ndev->mgmt_chann, msg);
	if (ret == -ETIME) {
		xdna_mailbox_stop_channel(ndev->mgmt_chann);
		xdna_mailbox_destroy_channel(ndev->mgmt_chann);
		ndev->mgmt_chann = NULL;
	}

	if (!ret && *hdl->data != AIE2_STATUS_SUCCESS) {
		XDNA_ERR(xdna, "command opcode 0x%x failed, status 0x%x",
			 msg->opcode, *hdl->data);
		ret = -EINVAL;
	}

	return ret;
}

int aie2_suspend_fw(struct amdxdna_dev_hdl *ndev)
{
	DECLARE_AIE2_MSG(suspend, MSG_OP_SUSPEND);

	return aie2_send_mgmt_msg_wait(ndev, &msg);
}

int aie2_resume_fw(struct amdxdna_dev_hdl *ndev)
{
	DECLARE_AIE2_MSG(suspend, MSG_OP_RESUME);

	return aie2_send_mgmt_msg_wait(ndev, &msg);
}

int aie2_set_runtime_cfg(struct amdxdna_dev_hdl *ndev, u32 type, u64 value)
{
	DECLARE_AIE2_MSG(set_runtime_cfg, MSG_OP_SET_RUNTIME_CONFIG);

	req.type = type;
	req.value = value;

	return aie2_send_mgmt_msg_wait(ndev, &msg);
}

int aie2_get_runtime_cfg(struct amdxdna_dev_hdl *ndev, u32 type, u64 *value)
{
	DECLARE_AIE2_MSG(get_runtime_cfg, MSG_OP_GET_RUNTIME_CONFIG);
	int ret;

	req.type = type;
	ret = aie2_send_mgmt_msg_wait(ndev, &msg);
	if (ret) {
		XDNA_ERR(ndev->xdna, "Failed to get runtime config, ret %d", ret);
		return ret;
	}

	*value = resp.value;
	return 0;
}

int aie2_check_protocol_version(struct amdxdna_dev_hdl *ndev)
{
	DECLARE_AIE2_MSG(protocol_version, MSG_OP_GET_PROTOCOL_VERSION);
	struct amdxdna_dev *xdna = ndev->xdna;
	int ret;

	ret = aie2_send_mgmt_msg_wait(ndev, &msg);
	if (ret) {
		XDNA_ERR(xdna, "Failed to get protocol version, ret %d", ret);
		return ret;
	}

	if (resp.major != ndev->priv->protocol_major) {
		XDNA_ERR(xdna, "Incompatible firmware protocol version major %d minor %d",
			 resp.major, resp.minor);
		return -EINVAL;
	}

	/*
	 * Greater protocol minor version means new messages/status/emun are
	 * added into the firmware interface protocol.
	 */
	if (resp.minor < ndev->priv->protocol_minor) {
		XDNA_ERR(xdna, "Firmware minor version smaller than supported");
		return -EINVAL;
	}

	return 0;
}

int aie2_assign_mgmt_pasid(struct amdxdna_dev_hdl *ndev, u16 pasid)
{
	DECLARE_AIE2_MSG(assign_mgmt_pasid, MSG_OP_ASSIGN_MGMT_PASID);

	req.pasid = pasid;

	return aie2_send_mgmt_msg_wait(ndev, &msg);
}

int aie2_query_aie_version(struct amdxdna_dev_hdl *ndev, struct aie_version *version)
{
	DECLARE_AIE2_MSG(aie_version_info, MSG_OP_QUERY_AIE_VERSION);
	struct amdxdna_dev *xdna = ndev->xdna;
	int ret;

	ret = aie2_send_mgmt_msg_wait(ndev, &msg);
	if (ret)
		return ret;

	XDNA_DBG(xdna, "Query AIE version - major: %u minor: %u completed",
		 resp.major, resp.minor);

	version->major = resp.major;
	version->minor = resp.minor;

	return 0;
}

int aie2_query_aie_metadata(struct amdxdna_dev_hdl *ndev, struct aie_metadata *metadata)
{
	DECLARE_AIE2_MSG(aie_tile_info, MSG_OP_QUERY_AIE_TILE_INFO);
	int ret;

	ret = aie2_send_mgmt_msg_wait(ndev, &msg);
	if (ret)
		return ret;

	metadata->size = resp.info.size;
	metadata->cols = resp.info.cols;
	metadata->rows = resp.info.rows;

	metadata->version.major = resp.info.major;
	metadata->version.minor = resp.info.minor;

	metadata->core.row_count = resp.info.core_rows;
	metadata->core.row_start = resp.info.core_row_start;
	metadata->core.dma_channel_count = resp.info.core_dma_channels;
	metadata->core.lock_count = resp.info.core_locks;
	metadata->core.event_reg_count = resp.info.core_events;

	metadata->mem.row_count = resp.info.mem_rows;
	metadata->mem.row_start = resp.info.mem_row_start;
	metadata->mem.dma_channel_count = resp.info.mem_dma_channels;
	metadata->mem.lock_count = resp.info.mem_locks;
	metadata->mem.event_reg_count = resp.info.mem_events;

	metadata->shim.row_count = resp.info.shim_rows;
	metadata->shim.row_start = resp.info.shim_row_start;
	metadata->shim.dma_channel_count = resp.info.shim_dma_channels;
	metadata->shim.lock_count = resp.info.shim_locks;
	metadata->shim.event_reg_count = resp.info.shim_events;

	return 0;
}

int aie2_query_firmware_version(struct amdxdna_dev_hdl *ndev,
				struct amdxdna_fw_ver *fw_ver)
{
	DECLARE_AIE2_MSG(firmware_version, MSG_OP_GET_FIRMWARE_VERSION);
	int ret;

	ret = aie2_send_mgmt_msg_wait(ndev, &msg);
	if (ret)
		return ret;

	fw_ver->major = resp.major;
	fw_ver->minor = resp.minor;
	fw_ver->sub = resp.sub;
	fw_ver->build = resp.build;

	return 0;
}

int aie2_create_context(struct amdxdna_dev_hdl *ndev, struct amdxdna_hwctx *hwctx)
{
	DECLARE_AIE2_MSG(create_ctx, MSG_OP_CREATE_CONTEXT);
	struct amdxdna_dev *xdna = ndev->xdna;
	struct xdna_mailbox_chann_res x2i;
	struct xdna_mailbox_chann_res i2x;
	struct cq_pair *cq_pair;
	u32 intr_reg;
	int ret;

	req.aie_type = 1;
	req.start_col = hwctx->start_col;
	req.num_col = hwctx->num_col;
	req.num_cq_pairs_requested = 1;
	req.pasid = hwctx->client->pasid;
	req.context_priority = 2;

	ret = aie2_send_mgmt_msg_wait(ndev, &msg);
	if (ret)
		return ret;

	hwctx->fw_ctx_id = resp.context_id;
	WARN_ONCE(hwctx->fw_ctx_id == -1, "Unexpected context id");

	cq_pair = &resp.cq_pair[0];
	x2i.mb_head_ptr_reg = AIE2_MBOX_OFF(ndev, cq_pair->x2i_q.head_addr);
	x2i.mb_tail_ptr_reg = AIE2_MBOX_OFF(ndev, cq_pair->x2i_q.tail_addr);
	x2i.rb_start_addr   = AIE2_SRAM_OFF(ndev, cq_pair->x2i_q.buf_addr);
	x2i.rb_size	    = cq_pair->x2i_q.buf_size;

	i2x.mb_head_ptr_reg = AIE2_MBOX_OFF(ndev, cq_pair->i2x_q.head_addr);
	i2x.mb_tail_ptr_reg = AIE2_MBOX_OFF(ndev, cq_pair->i2x_q.tail_addr);
	i2x.rb_start_addr   = AIE2_SRAM_OFF(ndev, cq_pair->i2x_q.buf_addr);
	i2x.rb_size	    = cq_pair->i2x_q.buf_size;

	ret = pci_irq_vector(to_pci_dev(xdna->ddev.dev), resp.msix_id);
	if (ret == -EINVAL) {
		XDNA_ERR(xdna, "not able to create channel");
		goto out_destroy_context;
	}

	intr_reg = i2x.mb_head_ptr_reg + 4;
	hwctx->priv->mbox_chann = xdna_mailbox_create_channel(ndev->mbox, &x2i, &i2x,
							      intr_reg, ret);
	if (!hwctx->priv->mbox_chann) {
		XDNA_ERR(xdna, "not able to create channel");
		ret = -EINVAL;
		goto out_destroy_context;
	}

	XDNA_DBG(xdna, "%s mailbox channel irq: %d, msix_id: %d",
		 hwctx->name, ret, resp.msix_id);
	XDNA_DBG(xdna, "%s created fw ctx %d pasid %d", hwctx->name,
		 hwctx->fw_ctx_id, hwctx->client->pasid);

	return 0;

out_destroy_context:
	aie2_destroy_context(ndev, hwctx);
	return ret;
}

int aie2_destroy_context(struct amdxdna_dev_hdl *ndev, struct amdxdna_hwctx *hwctx)
{
	DECLARE_AIE2_MSG(destroy_ctx, MSG_OP_DESTROY_CONTEXT);
	struct amdxdna_dev *xdna = ndev->xdna;
	int ret;

	if (hwctx->fw_ctx_id == -1)
		return 0;

	xdna_mailbox_stop_channel(hwctx->priv->mbox_chann);

	req.context_id = hwctx->fw_ctx_id;
	ret = aie2_send_mgmt_msg_wait(ndev, &msg);
	if (ret)
		XDNA_WARN(xdna, "%s destroy context failed, ret %d",
			  hwctx->name, ret);

	xdna_mailbox_destroy_channel(hwctx->priv->mbox_chann);
	XDNA_DBG(xdna, "%s destroyed fw ctx %d", hwctx->name,
		 hwctx->fw_ctx_id);
	hwctx->priv->mbox_chann = NULL;
	hwctx->fw_ctx_id = -1;

	return ret;
}

int aie2_map_host_buf(struct amdxdna_dev_hdl *ndev, u32 context_id, u64 addr, u64 size)
{
	DECLARE_AIE2_MSG(map_host_buffer, MSG_OP_MAP_HOST_BUFFER);
	struct amdxdna_dev *xdna = ndev->xdna;
	int ret;

	req.context_id = context_id;
	req.buf_addr = addr;
	req.buf_size = size;
	ret = aie2_send_mgmt_msg_wait(ndev, &msg);
	if (ret)
		return ret;

	XDNA_DBG(xdna, "fw ctx %d map host buf addr 0x%llx size 0x%llx",
		 context_id, addr, size);

	return 0;
}

#if defined(CONFIG_DEBUG_FS)
int aie2_self_test(struct amdxdna_dev_hdl *ndev)
{
	DECLARE_AIE2_MSG(check_self_test, MSG_OP_INVOKE_SELF_TEST);

	req.test_mask = 0x3F;
	return aie2_send_mgmt_msg_wait(ndev, &msg);
}
#else
int aie2_self_test(struct amdxdna_dev_hdl *ndev)
{
}
#endif

int aie2_query_status(struct amdxdna_dev_hdl *ndev, char __user *buf,
		      u32 size, u32 *cols_filled)
{
	DECLARE_AIE2_MSG(aie_column_info, MSG_OP_QUERY_COL_STATUS);
	struct amdxdna_dev *xdna = ndev->xdna;
	struct amdxdna_client *client;
	struct amdxdna_hwctx *hwctx;
	dma_addr_t dma_addr;
	u32 aie_bitmap = 0;
	u8 *buff_addr;
	int next = 0;
	int ret, idx;

	buff_addr = dma_alloc_noncoherent(xdna->ddev.dev, size, &dma_addr,
					  DMA_FROM_DEVICE, GFP_KERNEL);
	if (!buff_addr)
		return -ENOMEM;

	/* Go through each hardware context and mark the AIE columns that are active */
	list_for_each_entry(client, &xdna->client_list, node) {
		idx = srcu_read_lock(&client->hwctx_srcu);
		idr_for_each_entry_continue(&client->hwctx_idr, hwctx, next)
			aie_bitmap |= amdxdna_hwctx_col_map(hwctx);
		srcu_read_unlock(&client->hwctx_srcu, idx);
	}

	*cols_filled = 0;
	req.dump_buff_addr = dma_addr;
	req.dump_buff_size = size;
	req.num_cols = hweight32(aie_bitmap);
	req.aie_bitmap = aie_bitmap;

	drm_clflush_virt_range(buff_addr, size); /* device can access */
	ret = aie2_send_mgmt_msg_wait(ndev, &msg);
	if (ret) {
		XDNA_ERR(xdna, "Error during NPU query, status %d", ret);
		goto fail;
	}

	if (resp.status != AIE2_STATUS_SUCCESS) {
		XDNA_ERR(xdna, "Query NPU status failed, status 0x%x", resp.status);
		ret = -EINVAL;
		goto fail;
	}
	XDNA_DBG(xdna, "Query NPU status completed");

	if (size < resp.size) {
		ret = -EINVAL;
		XDNA_ERR(xdna, "Bad buffer size. Available: %u. Needs: %u", size, resp.size);
		goto fail;
	}

	if (copy_to_user(buf, buff_addr, resp.size)) {
		ret = -EFAULT;
		XDNA_ERR(xdna, "Failed to copy NPU status to user space");
		goto fail;
	}

	*cols_filled = aie_bitmap;

fail:
	dma_free_noncoherent(xdna->ddev.dev, size, buff_addr, dma_addr, DMA_FROM_DEVICE);
	return ret;
}

int aie2_register_asyn_event_msg(struct amdxdna_dev_hdl *ndev, dma_addr_t addr, u32 size,
				 void *handle, int (*cb)(void*, const u32 *, size_t))
{
	struct async_event_msg_req req = { 0 };
	struct xdna_mailbox_msg msg = {
		.send_data = (u8 *)&req,
		.send_size = sizeof(req),
		.handle = handle,
		.opcode = MSG_OP_REGISTER_ASYNC_EVENT_MSG,
		.notify_cb = cb,
	};

	req.buf_addr = addr;
	req.buf_size = size;

	XDNA_DBG(ndev->xdna, "Register addr 0x%llx size 0x%x", addr, size);
	return xdna_mailbox_send_msg(ndev->mgmt_chann, &msg, TX_TIMEOUT);
}

/* Below messages are to hardware context mailbox channel */
int aie2_config_cu(struct amdxdna_hwctx *hwctx)
{
	struct mailbox_channel *chann = hwctx->priv->mbox_chann;
	struct amdxdna_dev *xdna = hwctx->client->xdna;
	u32 shift = xdna->dev_info->dev_mem_buf_shift;
	DECLARE_AIE2_MSG(config_cu, MSG_OP_CONFIG_CU);
	struct drm_gem_object *gobj;
	struct amdxdna_gem_obj *abo;
	int ret, i;

	if (!chann)
		return -ENODEV;

	if (hwctx->cus->num_cus > MAX_NUM_CUS) {
		XDNA_DBG(xdna, "Exceed maximum CU %d", MAX_NUM_CUS);
		return -EINVAL;
	}

	for (i = 0; i < hwctx->cus->num_cus; i++) {
		struct amdxdna_cu_config *cu = &hwctx->cus->cu_configs[i];

		gobj = drm_gem_object_lookup(hwctx->client->filp, cu->cu_bo);
		if (!gobj) {
			XDNA_ERR(xdna, "Lookup GEM object failed");
			return -EINVAL;
		}
		abo = to_xdna_obj(gobj);

		if (abo->type != AMDXDNA_BO_DEV) {
			drm_gem_object_put(gobj);
			XDNA_ERR(xdna, "Invalid BO type");
			return -EINVAL;
		}

		req.cfgs[i].pdi_addr = abo->mem.dev_addr >> shift;
		req.cfgs[i].cu_func = cu->cu_func;
		XDNA_DBG(xdna, "CU %d full addr 0x%llx, short addr 0x%x, cu func %d", i,
			 abo->mem.dev_addr, req.cfgs[i].pdi_addr, req.cfgs[i].cu_func);
		drm_gem_object_put(gobj);
	}
	req.num_cus = hwctx->cus->num_cus;

	ret = xdna_send_msg_wait(xdna, chann, &msg);
	if (ret == -ETIME)
		aie2_destroy_context(xdna->dev_handle, hwctx);

	if (resp.status == AIE2_STATUS_SUCCESS) {
		XDNA_DBG(xdna, "Configure %d CUs, ret %d", req.num_cus, ret);
		return 0;
	}

	XDNA_ERR(xdna, "Command opcode 0x%x failed, status 0x%x ret %d",
		 msg.opcode, resp.status, ret);
	return ret;
}

int aie2_execbuf(struct amdxdna_hwctx *hwctx, struct amdxdna_sched_job *job,
		 void *handle, int (*notify_cb)(void *, const u32 *, size_t))
{
	struct mailbox_channel *chann = hwctx->priv->mbox_chann;
	struct amdxdna_dev *xdna = hwctx->client->xdna;
	union {
		struct execute_buffer_req ebuf;
		struct exec_dpu_req dpu;
	} req;
	struct xdna_mailbox_msg msg;
	u32 payload_len;
	void *payload;
	int cu_idx;
	int ret;
	u32 op;

	if (!chann)
		return -ENODEV;

	payload = amdxdna_cmd_get_payload(job, 0, &payload_len);
	if (!payload) {
		XDNA_ERR(xdna, "Invalid command, cannot get payload");
		return -EINVAL;
	}

	cu_idx = amdxdna_cmd_get_cu_idx(job, 0);
	if (cu_idx < 0) {
		XDNA_DBG(xdna, "Invalid cu idx");
		return -EINVAL;
	}

	op = amdxdna_cmd_get_op(job, 0);
	switch (op) {
	case ERT_START_CU:
		if (unlikely(payload_len > sizeof(req.ebuf.payload)))
			XDNA_DBG(xdna, "Invalid ebuf payload len: %d", payload_len);
		req.ebuf.cu_idx = cu_idx;
		memcpy(req.ebuf.payload, payload, sizeof(req.ebuf.payload));
		msg.send_size = sizeof(req.ebuf);
		msg.opcode = MSG_OP_EXECUTE_BUFFER_CF;
		break;
	case ERT_START_DPU: {
		struct amdxdna_cmd_start_dpu *sd = payload;

		if (sd->chained) {
			XDNA_DBG(xdna, "Chained ERT_START_DPU is not supported");
			return -EOPNOTSUPP;
		}
		if (unlikely(payload_len - sizeof(*sd) > sizeof(req.dpu.payload)))
			XDNA_DBG(xdna, "Invalid dpu payload len: %d", payload_len);
		req.dpu.inst_buf_addr = sd->instruction_buffer;
		req.dpu.inst_size = sd->instruction_buffer_size;
		req.dpu.inst_prop_cnt = 0;
		req.dpu.cu_idx = cu_idx;
		memcpy(req.dpu.payload, ((char *)payload) + sizeof(*sd),
		       sizeof(req.dpu.payload));
		msg.send_size = sizeof(req.dpu);
		msg.opcode = MSG_OP_EXEC_DPU;
		break;
	}
	default:
		XDNA_DBG(xdna, "Invalid ERT cmd op code: %d", op);
		return -EINVAL;
	}
	msg.handle = handle;
	msg.notify_cb = notify_cb;
	msg.send_data = (u8 *)&req;

	ret = xdna_mailbox_send_msg(chann, &msg, TX_TIMEOUT);
	if (ret) {
		XDNA_ERR(xdna, "Send message failed");
		return ret;
	}

	return 0;
}

static inline int
aie2_cmdlist_fill_slot_cf(void *cmd_buf,
			  struct amdxdna_sched_job *job, u32 *size)
{
	struct cmd_chain_slot_execbuf_cf *buf = cmd_buf;
	u32 payload_len;
	void *payload;
	int cu_idx;
	int i;

	*size = 0;
	for (i = 0; i < job->cmd_bo_cnt; i++) {
		payload = amdxdna_cmd_get_payload(job, i, &payload_len);
		if (!payload)
			return -EINVAL;

		cu_idx = amdxdna_cmd_get_cu_idx(job, i);
		if (cu_idx < 0)
			return -EINVAL;

		if (!slot_cf_has_space(*size, payload_len))
			return -ENOSPC;

		buf->cu_idx = cu_idx;
		buf->arg_cnt = payload_len / sizeof(u32);
		memcpy(buf->args, payload, payload_len);

		/* Accurate buf size to hint firmware to do necessary copy */
		*size += sizeof(*buf) + payload_len;
		buf = (struct cmd_chain_slot_execbuf_cf *)((char *)cmd_buf + *size);
	}

	return 0;
}

static inline int
aie2_cmdlist_fill_slot_dpu(void *cmd_buf, struct amdxdna_sched_job *job, u32 *size)
{
	struct cmd_chain_slot_dpu *buf = cmd_buf;
	struct amdxdna_cmd_start_dpu *sd;
	u32 dpu_arg_size;
	u32 payload_len;
	void *payload;
	int cu_idx;
	int i;

	*size = 0;
	for (i = 0; i < job->cmd_bo_cnt; i++) {
		payload = amdxdna_cmd_get_payload(job, i, &payload_len);
		sd = payload;

		dpu_arg_size = payload_len - sizeof(*sd);
		if (payload_len < sizeof(*sd) || dpu_arg_size > MAX_DPU_ARGS_SIZE)
			return -EINVAL;

		cu_idx = amdxdna_cmd_get_cu_idx(job, i);
		if (cu_idx < 0)
			return -EINVAL;

		if (!slot_dpu_has_space(*size, dpu_arg_size))
			return -ENOSPC;

		buf->inst_buf_addr = sd->instruction_buffer;
		buf->inst_size = sd->instruction_buffer_size;
		buf->inst_prop_cnt = 0;
		buf->cu_idx = cu_idx;
		buf->arg_cnt = dpu_arg_size / sizeof(u32);
		memcpy(buf->args, ((char *)payload + sizeof(*sd)), dpu_arg_size);

		/* Accurate buf size to hint firmware to do necessary copy */
		*size += sizeof(*buf) + dpu_arg_size;
		buf = (struct cmd_chain_slot_dpu *)((char *)cmd_buf + *size);
	}

	return 0;
}

int aie2_cmdlist(struct amdxdna_hwctx *hwctx, struct amdxdna_sched_job *job,
		 void *handle, int (*notify_cb)(void *, const u32 *, size_t))
{
	struct mailbox_channel *chann = hwctx->priv->mbox_chann;
	struct amdxdna_dev *xdna = hwctx->client->xdna;
	struct xdna_mailbox_msg msg;
	struct amdxdna_gem_obj *abo;
	struct cmd_chain_req req;
	int idx, ret;
	u32 op;

	if (!chann)
		return -ENODEV;

	idx = get_job_idx(job->seq);
	abo = hwctx->priv->cmd_buf[idx];
	req.buf_addr = abo->mem.dev_addr;
	req.count = job->cmd_bo_cnt;

	op = amdxdna_cmd_get_op(job, 0);
	switch (op) {
	case ERT_START_CU:
		ret = aie2_cmdlist_fill_slot_cf(abo->mem.kva, job, &req.buf_size);
		break;
	case ERT_START_DPU:
		ret = aie2_cmdlist_fill_slot_dpu(abo->mem.kva, job, &req.buf_size);
		break;
	default:
		ret = -EOPNOTSUPP;
	}
	if (ret) {
		XDNA_ERR(xdna, "Failed to handle cmd op %d ret %d", op, ret);
		return ret;
	}

	XDNA_DBG(xdna, "Command buf addr 0x%llx size 0x%x count %d",
		 req.buf_addr, req.buf_size, req.count);

	drm_clflush_virt_range(abo->mem.kva, req.buf_size);
	/* Device can access the buf after flush */

	msg.handle = handle;
	msg.notify_cb = notify_cb;
	msg.send_data = (u8 *)&req;
	msg.send_size = sizeof(req);
	msg.opcode = MSG_OP_CHAIN_EXEC_BUFFER_CF;

	ret = xdna_mailbox_send_msg(chann, &msg, TX_TIMEOUT);
	if (ret) {
		XDNA_ERR(xdna, "Send message failed");
		return ret;
	}

	return 0;
}

int aie2_sync_bo(struct amdxdna_hwctx *hwctx, struct amdxdna_sched_job *job,
		 void *handle, int (*notify_cb)(void *, const u32 *, size_t))
{
	struct mailbox_channel *chann = hwctx->priv->mbox_chann;
	struct amdxdna_gem_obj *abo = to_xdna_obj(job->bos[0]);
	struct amdxdna_dev *xdna = hwctx->client->xdna;
	struct xdna_mailbox_msg msg;
	struct sync_bo_req req;
	int ret = 0;

	req.src_addr = 0;
	req.dst_addr = abo->mem.dev_addr - hwctx->client->dev_heap->mem.dev_addr;
	req.size = abo->mem.size;

	/* Device to Host */
	req.src_type = SYNC_BO_DEV_MEM;
	req.dst_type = SYNC_BO_HOST_MEM;

	XDNA_DBG(xdna, "sync %d bytes src(0x%llx) to dst(0x%llx) completed",
		 req.size, req.src_addr, req.dst_addr);

	msg.handle = handle;
	msg.notify_cb = notify_cb;
	msg.send_data = (u8 *)&req;
	msg.send_size = sizeof(req);
	msg.opcode = MSG_OP_SYNC_BO;

	ret = xdna_mailbox_send_msg(chann, &msg, TX_TIMEOUT);
	if (ret) {
		XDNA_ERR(xdna, "Send message failed");
		return ret;
	}

	return 0;
}

#ifdef AMDXDNA_DEVEL
int aie2_register_pdis(struct amdxdna_hwctx *hwctx)
{
	DECLARE_AIE2_MSG(register_pdi, MSG_OP_REGISTER_PDI);
	struct amdxdna_dev *xdna = hwctx->client->xdna;
	struct amdxdna_dev_hdl *ndev = xdna->dev_handle;
	int num_cus = hwctx->cus->num_cus;
	struct drm_gem_object *gobj;
	struct amdxdna_gem_obj *abo;
	struct hwctx_pdi *pdi;
	int i, ret;

	if (num_cus > MAX_NUM_CUS) {
		XDNA_DBG(xdna, "Exceed maximum CU %d", MAX_NUM_CUS);
		return -EINVAL;
	}

	hwctx->priv->pdi_infos = kcalloc(num_cus, sizeof(*hwctx->priv->pdi_infos), GFP_KERNEL);
	if (!hwctx->priv->pdi_infos)
		return -ENOMEM;

	req.num_infos = 1;
	for (i = 0; i < num_cus; i++) {
		struct amdxdna_cu_config *cu = &hwctx->cus->cu_configs[i];

		pdi = &hwctx->priv->pdi_infos[i];
		gobj = drm_gem_object_lookup(hwctx->client->filp, cu->cu_bo);
		if (!gobj) {
			XDNA_ERR(xdna, "Lookup GEM object failed");
			ret = -EINVAL;
			goto cleanup;
		}
		abo = to_xdna_obj(gobj);

		if (abo->type != AMDXDNA_BO_DEV) {
			drm_gem_object_put(gobj);
			XDNA_ERR(xdna, "Invalid BO type");
			ret = -EINVAL;
			goto cleanup;
		}

		pdi->id = -1; /* Set to negative value, so that cleanup can work */
		pdi->id = ida_alloc_range(&xdna->pdi_ida, 0, AIE2_MAX_PDI_ID, GFP_KERNEL);
		if (pdi->id < 0) {
			XDNA_ERR(xdna, "Cannot allocate PDI id");
			ret = pdi->id;
			goto cleanup;
		}
		pdi->size = gobj->size;
		pdi->addr = dma_alloc_noncoherent(xdna->ddev.dev, pdi->size, &pdi->dma_addr,
						  DMA_TO_DEVICE, GFP_KERNEL);
		if (!pdi->addr) {
			drm_gem_object_put(gobj);
			ret = -ENOMEM;
			goto cleanup;
		}

		if (copy_from_user(pdi->addr, u64_to_user_ptr(abo->mem.userptr), pdi->size)) {
			drm_gem_object_put(gobj);
			ret = -EFAULT;
			goto cleanup;
		}

		drm_gem_object_put(gobj);
		req.pdi_info.pdi_id = pdi->id;
		req.pdi_info.address = pdi->dma_addr;
		req.pdi_info.size = pdi->size;
		req.pdi_info.type = 3;
		resp.status = MAX_AIE2_STATUS_CODE;

		drm_clflush_virt_range(pdi->addr, pdi->size); /* device can access */
		ret = aie2_send_mgmt_msg_wait(ndev, &msg);
		if (ret) {
			XDNA_ERR(xdna, "PDI %d register failed, ret %d", pdi->id, ret);
			goto cleanup;
		}

		pdi->registered = 1;
		WARN_ONCE(pdi->id != resp.reg_index, "PDI ID and FW registered index mismatch");
		XDNA_DBG(xdna, "PDI %d register completed, index %d", pdi->id, resp.reg_index);
	}

	return 0;

cleanup:
	aie2_unregister_pdis(hwctx);
	return ret;
}

int aie2_unregister_pdis(struct amdxdna_hwctx *hwctx)
{
	DECLARE_AIE2_MSG(unregister_pdi, MSG_OP_UNREGISTER_PDI);
	struct amdxdna_dev *xdna = hwctx->client->xdna;
	struct amdxdna_dev_hdl *ndev = xdna->dev_handle;
	int num_cus = hwctx->cus->num_cus;
	struct hwctx_pdi *pdi;
	int ret, i;

	req.num_pdi = 1;
	for (i = 0; i < num_cus; i++) {
		pdi = &hwctx->priv->pdi_infos[i];

		if (pdi->registered) {
			req.pdi_id = pdi->id;
			resp.status = MAX_AIE2_STATUS_CODE;
			ret = aie2_send_mgmt_msg_wait(ndev, &msg);
			if (ret) {
				XDNA_ERR(xdna, "PDI %d unregister failed, ret %d",
					 pdi->id, ret);
				break;
			}

			pdi->registered = 0;
			XDNA_DBG(xdna, "PDI %d unregister completed", pdi->id);
		}

		if (pdi->addr)
			dma_free_noncoherent(xdna->ddev.dev, pdi->size, pdi->addr,
					     pdi->dma_addr, DMA_TO_DEVICE);

		if (pdi->id >= 0)
			ida_free(&xdna->pdi_ida, pdi->id);
	}

	kfree(hwctx->priv->pdi_infos);
	return 0;
}

int aie2_legacy_config_cu(struct amdxdna_hwctx *hwctx)
{
	DECLARE_AIE2_MSG(legacy_config_cu, MSG_OP_LEGACY_CONFIG_CU);
	struct mailbox_channel *chann = hwctx->priv->mbox_chann;
	struct amdxdna_dev *xdna = hwctx->client->xdna;
	int ret, i;

	if (!chann)
		return -ENODEV;

	if (hwctx->cus->num_cus > MAX_NUM_CUS) {
		XDNA_DBG(xdna, "Exceed maximum CU %d", MAX_NUM_CUS);
		return -EINVAL;
	}

	req.num_cus = hwctx->cus->num_cus;
	for (i = 0; i < req.num_cus; i++) {
		struct amdxdna_cu_config *cu = &hwctx->cus->cu_configs[i];

		req.configs[i].cu_idx = i;
		req.configs[i].cu_func = cu->cu_func;
		req.configs[i].cu_pdi_id = hwctx->priv->pdi_infos[i].id;
	}

	ret = xdna_send_msg_wait(xdna, chann, &msg);
	if (ret == -ETIME)
		aie2_destroy_context(xdna->dev_handle, hwctx);

	XDNA_DBG(xdna, "Configure %d CUs, ret %d", req.num_cus, ret);

	return ret;
}
#endif