#include <linux/completion.h>
#include <linux/delay.h>
#include <linux/fs.h>
#include <linux/kernel.h>
#include <linux/list.h>
#include <linux/miscdevice.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/net.h>
#include <linux/slab.h>
#include <linux/socket.h>
#include <linux/uaccess.h>
#include <linux/un.h>
#include <linux/vmalloc.h>

#include <net/sock.h>

#include "protogpu_ioctl.h"

struct protogpu_bo {
	u64 handle;
	u64 mmap_offset;
	u64 bytes;
	u32 flags;
	void *backing;
	struct list_head link;
};

struct protogpu_job {
	u64 id;
	u32 status;
	u64 steps;
	char *diag;
	size_t diag_len;
	struct completion done;
	struct list_head link;
};

struct protogpu_ctx {
	u64 id;
	u64 next_bo_handle;
	u64 next_job_id;
	struct list_head bo_list;
	struct list_head job_list;
	struct list_head link;
};

struct protogpu_file {
	struct mutex lock;
	u64 next_ctx_id;
	struct list_head ctx_list;
	char broker_sock[PROTOGPU_IOCTL_FD_PATH_MAX];
};

enum {
	PROTOGPU_WIRE_MAGIC = 0x50544750u,
	PROTOGPU_WIRE_VERSION = 1u,
	PROTOGPU_WIRE_REQUEST = 1u,
	PROTOGPU_WIRE_RESPONSE = 2u,
};

struct protogpu_wire_header {
	u32 magic;
	u32 version;
	u32 type;
	u32 reserved0;
	u64 payload_bytes;
};

struct protogpu_wire_request_meta {
	struct protogpu_submit_job job;
	u32 buffer_count;
	u32 reserved0;
	u64 arena_bytes;
};

struct protogpu_wire_buffer_desc {
	u64 handle;
	u32 flags;
	u32 reserved0;
	u64 bytes;
};

struct protogpu_wire_response_meta {
	u32 status;
	u32 reserved0;
	u64 steps;
	u64 diag_bytes;
	u64 trace_bytes;
	u64 stats_bytes;
	u32 buffer_count;
	u32 reserved1;
};

static struct protogpu_ctx *find_ctx_locked(struct protogpu_file *pf, u64 ctx_id)
{
	struct protogpu_ctx *ctx;

	list_for_each_entry(ctx, &pf->ctx_list, link) {
		if (ctx->id == ctx_id)
			return ctx;
	}
	return NULL;
}

static struct protogpu_bo *find_bo_locked(struct protogpu_ctx *ctx, u64 handle)
{
	struct protogpu_bo *bo;

	list_for_each_entry(bo, &ctx->bo_list, link) {
		if (bo->handle == handle)
			return bo;
	}
	return NULL;
}

static struct protogpu_bo *find_bo_by_offset_locked(struct protogpu_file *pf, u64 mmap_offset)
{
	struct protogpu_ctx *ctx;
	struct protogpu_bo *bo;

	list_for_each_entry(ctx, &pf->ctx_list, link) {
		list_for_each_entry(bo, &ctx->bo_list, link) {
			if (bo->mmap_offset == mmap_offset)
				return bo;
		}
	}
	return NULL;
}

static struct protogpu_job *find_job_locked(struct protogpu_ctx *ctx, u64 job_id)
{
	struct protogpu_job *job;

	list_for_each_entry(job, &ctx->job_list, link) {
		if (job->id == job_id)
			return job;
	}
	return NULL;
}

static bool checked_span(u64 offset, u64 bytes, u64 total)
{
	u64 end;

	if (offset > total)
		return false;
	if (bytes > total)
		return false;
	end = offset + bytes;
	if (end < offset)
		return false;
	return end <= total;
}

static int checked_size_add(size_t *accum, size_t value)
{
	if (value > SIZE_MAX - *accum)
		return -EOVERFLOW;
	*accum += value;
	return 0;
}

static int sock_write_full(struct socket *sock, const void *data, size_t bytes)
{
	struct msghdr msg = {};
	struct kvec iov;
	const u8 *ptr = data;
	size_t done = 0;

	while (done < bytes) {
		ssize_t rc;

		iov.iov_base = (void *)(ptr + done);
		iov.iov_len = bytes - done;
		rc = kernel_sendmsg(sock, &msg, &iov, 1, iov.iov_len);
		if (rc < 0) {
			if (rc == -EINTR)
				continue;
			return rc;
		}
		if (rc == 0)
			return -EPIPE;
		done += rc;
	}

	return 0;
}

static int sock_read_full(struct socket *sock, void *data, size_t bytes)
{
	struct msghdr msg = {};
	struct kvec iov;
	u8 *ptr = data;
	size_t done = 0;

	while (done < bytes) {
		ssize_t rc;

		iov.iov_base = ptr + done;
		iov.iov_len = bytes - done;
		rc = kernel_recvmsg(sock, &msg, &iov, 1, iov.iov_len, 0);
		if (rc < 0) {
			if (rc == -EINTR)
				continue;
			return rc;
		}
		if (rc == 0)
			return -EPIPE;
		done += rc;
	}

	return 0;
}

static int submit_to_broker_locked(const char *broker_sock,
					   const struct protogpu_ioctl_submit_job *req,
					   const u8 *arena,
					   struct protogpu_buffer_binding *bindings,
					   struct protogpu_bo **bound_bos,
					   struct protogpu_job *job)
{
	struct socket *sock = NULL;
	struct sockaddr_un addr;
	struct protogpu_wire_header req_hdr = {};
	struct protogpu_wire_request_meta req_meta = {};
	struct protogpu_wire_header resp_hdr = {};
	struct protogpu_wire_response_meta resp_meta = {};
	struct protogpu_wire_buffer_desc *req_descs = NULL;
	struct protogpu_wire_buffer_desc *resp_descs = NULL;
	u8 *req_payload = NULL;
	u8 *resp_payload = NULL;
	size_t req_payload_bytes = 0;
	size_t resp_payload_bytes;
	size_t cursor;
	u64 i;
	int rc;

	if (broker_sock[0] == '\0') {
		job->status = PROTOGPU_STATUS_RUNTIME_ERROR;
		job->diag = kstrdup("broker socket not configured", GFP_KERNEL);
		if (!job->diag)
			return -ENOMEM;
		job->diag_len = strlen(job->diag);
		job->steps = 0;
		return 0;
	}

	rc = checked_size_add(&req_payload_bytes, sizeof(req_meta));
	if (rc)
		return rc;
	rc = checked_size_add(&req_payload_bytes,
			      (size_t)req->job.buffer_bindings.count * sizeof(*req_descs));
	if (rc)
		return rc;
	rc = checked_size_add(&req_payload_bytes, (size_t)req->arena_bytes);
	if (rc)
		return rc;
	for (i = 0; i < req->job.buffer_bindings.count; i++) {
		rc = checked_size_add(&req_payload_bytes,
				      (size_t)bound_bos[i]->bytes);
		if (rc)
			return rc;
	}

	if (req_payload_bytes > (64u * 1024u * 1024u))
		return -E2BIG;

	req_payload = kvzalloc(req_payload_bytes, GFP_KERNEL);
	if (!req_payload)
		return -ENOMEM;

	req_descs = kcalloc(req->job.buffer_bindings.count, sizeof(*req_descs), GFP_KERNEL);
	if (req->job.buffer_bindings.count != 0 && !req_descs) {
		rc = -ENOMEM;
		goto out;
	}

	req_meta.job = req->job;
	req_meta.buffer_count = req->job.buffer_bindings.count;
	req_meta.arena_bytes = req->arena_bytes;

	cursor = 0;
	memcpy(req_payload + cursor, &req_meta, sizeof(req_meta));
	cursor += sizeof(req_meta);

	for (i = 0; i < req->job.buffer_bindings.count; i++) {
		req_descs[i].handle = bindings[i].handle;
		req_descs[i].flags = bindings[i].flags;
		req_descs[i].bytes = bound_bos[i]->bytes;
	}
	if (req->job.buffer_bindings.count != 0) {
		size_t desc_bytes = (size_t)req->job.buffer_bindings.count * sizeof(*req_descs);
		memcpy(req_payload + cursor, req_descs, desc_bytes);
		cursor += desc_bytes;
	}

	if (req->arena_bytes != 0) {
		memcpy(req_payload + cursor, arena, (size_t)req->arena_bytes);
		cursor += (size_t)req->arena_bytes;
	}

	for (i = 0; i < req->job.buffer_bindings.count; i++) {
		if (bound_bos[i]->bytes != 0) {
			memcpy(req_payload + cursor, bound_bos[i]->backing,
			       (size_t)bound_bos[i]->bytes);
			cursor += (size_t)bound_bos[i]->bytes;
		}
	}

	if (cursor != req_payload_bytes) {
		rc = -EIO;
		goto out;
	}

	rc = sock_create_kern(&init_net, AF_UNIX, SOCK_STREAM, 0, &sock);
	if (rc)
		goto out;

	memset(&addr, 0, sizeof(addr));
	addr.sun_family = AF_UNIX;
	if (strscpy(addr.sun_path, broker_sock, sizeof(addr.sun_path)) < 0) {
		rc = -ENAMETOOLONG;
		goto out;
	}

	rc = kernel_connect(sock, (struct sockaddr_unsized *)&addr, sizeof(addr), 0);
	if (rc)
		goto out;

	req_hdr.magic = PROTOGPU_WIRE_MAGIC;
	req_hdr.version = PROTOGPU_WIRE_VERSION;
	req_hdr.type = PROTOGPU_WIRE_REQUEST;
	req_hdr.payload_bytes = req_payload_bytes;

	rc = sock_write_full(sock, &req_hdr, sizeof(req_hdr));
	if (rc)
		goto out;
	rc = sock_write_full(sock, req_payload, req_payload_bytes);
	if (rc)
		goto out;

	rc = sock_read_full(sock, &resp_hdr, sizeof(resp_hdr));
	if (rc)
		goto out;
	if (resp_hdr.magic != PROTOGPU_WIRE_MAGIC ||
	    resp_hdr.version != PROTOGPU_WIRE_VERSION ||
	    resp_hdr.type != PROTOGPU_WIRE_RESPONSE) {
		rc = -EPROTO;
		goto out;
	}

	resp_payload_bytes = (size_t)resp_hdr.payload_bytes;
	if ((u64)resp_payload_bytes != resp_hdr.payload_bytes ||
	    resp_payload_bytes > (64u * 1024u * 1024u)) {
		rc = -E2BIG;
		goto out;
	}

	resp_payload = kvzalloc(resp_payload_bytes, GFP_KERNEL);
	if (!resp_payload) {
		rc = -ENOMEM;
		goto out;
	}
	if (resp_payload_bytes != 0) {
		rc = sock_read_full(sock, resp_payload, resp_payload_bytes);
		if (rc)
			goto out;
	}

	if (resp_payload_bytes < sizeof(resp_meta)) {
		rc = -EPROTO;
		goto out;
	}

	cursor = 0;
	memcpy(&resp_meta, resp_payload + cursor, sizeof(resp_meta));
	cursor += sizeof(resp_meta);

	if (resp_meta.buffer_count != req->job.buffer_bindings.count) {
		rc = -EPROTO;
		goto out;
	}

	if (checked_size_add(&cursor,
			     (size_t)resp_meta.buffer_count * sizeof(*resp_descs)) != 0 ||
	    cursor > resp_payload_bytes) {
		rc = -EPROTO;
		goto out;
	}

	resp_descs = kcalloc(resp_meta.buffer_count, sizeof(*resp_descs), GFP_KERNEL);
	if (resp_meta.buffer_count != 0 && !resp_descs) {
		rc = -ENOMEM;
		goto out;
	}
	if (resp_meta.buffer_count != 0) {
		memcpy(resp_descs, resp_payload + sizeof(resp_meta),
		       (size_t)resp_meta.buffer_count * sizeof(*resp_descs));
	}

	cursor = sizeof(resp_meta) +
		 (size_t)resp_meta.buffer_count * sizeof(*resp_descs);

	if (checked_size_add(&cursor, (size_t)resp_meta.diag_bytes) != 0 ||
	    checked_size_add(&cursor, (size_t)resp_meta.trace_bytes) != 0 ||
	    checked_size_add(&cursor, (size_t)resp_meta.stats_bytes) != 0 ||
	    cursor > resp_payload_bytes) {
		rc = -EPROTO;
		goto out;
	}

	job->diag_len = (size_t)resp_meta.diag_bytes;
	job->diag = kmalloc(job->diag_len + 1, GFP_KERNEL);
	if (!job->diag) {
		rc = -ENOMEM;
		goto out;
	}
	memcpy(job->diag,
	       resp_payload + sizeof(resp_meta) +
	       (size_t)resp_meta.buffer_count * sizeof(*resp_descs),
	       job->diag_len);
	job->diag[job->diag_len] = '\0';

	cursor = sizeof(resp_meta) +
		 (size_t)resp_meta.buffer_count * sizeof(*resp_descs) +
		 (size_t)resp_meta.diag_bytes +
		 (size_t)resp_meta.trace_bytes +
		 (size_t)resp_meta.stats_bytes;

	for (i = 0; i < resp_meta.buffer_count; i++) {
		if (resp_descs[i].handle != bindings[i].handle ||
		    resp_descs[i].bytes != bound_bos[i]->bytes ||
		    checked_size_add(&cursor, (size_t)resp_descs[i].bytes) != 0 ||
		    cursor > resp_payload_bytes) {
			rc = -EPROTO;
			goto out;
		}
		if (resp_descs[i].bytes != 0) {
			size_t data_off = cursor - (size_t)resp_descs[i].bytes;
			memcpy(bound_bos[i]->backing, resp_payload + data_off,
			       (size_t)resp_descs[i].bytes);
		}
		bound_bos[i]->flags = resp_descs[i].flags;
	}

	if (cursor != resp_payload_bytes) {
		rc = -EPROTO;
		goto out;
	}

	job->status = resp_meta.status;
	job->steps = resp_meta.steps;
	rc = 0;

out:
	if (rc && !job->diag) {
		job->status = PROTOGPU_STATUS_RUNTIME_ERROR;
		job->diag = kasprintf(GFP_KERNEL, "broker transport failed: %d", rc);
		if (!job->diag)
			job->diag = kstrdup("broker transport failed", GFP_KERNEL);
		if (job->diag)
			job->diag_len = strlen(job->diag);
		job->steps = 0;
		rc = 0;
	}

	kfree(resp_descs);
	kvfree(resp_payload);
	kfree(req_descs);
	kvfree(req_payload);
	if (sock)
		sock_release(sock);
	return rc;
}

static void free_ctx(struct protogpu_ctx *ctx)
{
	struct protogpu_bo *bo, *bo_tmp;
	struct protogpu_job *job, *job_tmp;

	list_for_each_entry_safe(bo, bo_tmp, &ctx->bo_list, link) {
		list_del(&bo->link);
		if (bo->backing)
			vfree(bo->backing);
		kfree(bo);
	}

	list_for_each_entry_safe(job, job_tmp, &ctx->job_list, link) {
		list_del(&job->link);
		kfree(job->diag);
		kfree(job);
	}

	kfree(ctx);
}

static long handle_create_ctx(struct protogpu_file *pf, unsigned long arg)
{
	struct protogpu_ioctl_create_ctx req;
	struct protogpu_ctx *ctx;

	if (copy_from_user(&req, (void __user *)arg, sizeof(req)))
		return -EFAULT;
	if (req.uapi_version != PROTOGPU_IOCTL_UAPI_VERSION_V1)
		return -EINVAL;

	ctx = kzalloc(sizeof(*ctx), GFP_KERNEL);
	if (!ctx)
		return -ENOMEM;

	INIT_LIST_HEAD(&ctx->bo_list);
	INIT_LIST_HEAD(&ctx->job_list);
	ctx->next_bo_handle = 1;
	ctx->next_job_id = 1;

	mutex_lock(&pf->lock);
	ctx->id = pf->next_ctx_id++;
	list_add_tail(&ctx->link, &pf->ctx_list);
	mutex_unlock(&pf->lock);

	req.ctx_id = ctx->id;
	if (copy_to_user((void __user *)arg, &req, sizeof(req)))
		return -EFAULT;
	return 0;
}

static long handle_destroy_ctx(struct protogpu_file *pf, unsigned long arg)
{
	struct protogpu_ioctl_destroy_ctx req;
	struct protogpu_ctx *ctx;

	if (copy_from_user(&req, (void __user *)arg, sizeof(req)))
		return -EFAULT;

	mutex_lock(&pf->lock);
	ctx = find_ctx_locked(pf, req.ctx_id);
	if (!ctx) {
		mutex_unlock(&pf->lock);
		return -ENOENT;
	}
	list_del(&ctx->link);
	mutex_unlock(&pf->lock);

	free_ctx(ctx);
	return 0;
}

static long handle_set_broker(struct protogpu_file *pf, unsigned long arg)
{
	struct protogpu_ioctl_set_broker req;
	char tmp[PROTOGPU_IOCTL_FD_PATH_MAX + 1];

	if (copy_from_user(&req, (void __user *)arg, sizeof(req)))
		return -EFAULT;

	memcpy(tmp, req.unix_socket_path, sizeof(req.unix_socket_path));
	tmp[PROTOGPU_IOCTL_FD_PATH_MAX] = '\0';

	mutex_lock(&pf->lock);
	strscpy(pf->broker_sock, tmp, sizeof(pf->broker_sock));
	mutex_unlock(&pf->lock);
	return 0;
}

static long handle_alloc_bo(struct protogpu_file *pf, unsigned long arg)
{
	struct protogpu_ioctl_alloc_bo req;
	struct protogpu_ctx *ctx;
	struct protogpu_bo *bo;

	if (copy_from_user(&req, (void __user *)arg, sizeof(req)))
		return -EFAULT;
	if (req.bytes == 0)
		return -EINVAL;

	bo = kzalloc(sizeof(*bo), GFP_KERNEL);
	if (!bo)
		return -ENOMEM;
	bo->bytes = req.bytes;
	bo->flags = req.flags;
	bo->backing = vmalloc_user(req.bytes);
	if (!bo->backing) {
		kfree(bo);
		return -ENOMEM;
	}

	mutex_lock(&pf->lock);
	ctx = find_ctx_locked(pf, req.ctx_id);
	if (!ctx) {
		mutex_unlock(&pf->lock);
		kfree(bo);
		return -ENOENT;
	}

	bo->handle = ctx->next_bo_handle++;
	bo->mmap_offset = bo->handle << PAGE_SHIFT;
	list_add_tail(&bo->link, &ctx->bo_list);
	mutex_unlock(&pf->lock);

	req.handle = bo->handle;
	req.mmap_offset = bo->mmap_offset;
	if (copy_to_user((void __user *)arg, &req, sizeof(req)))
		return -EFAULT;
	return 0;
}

static long handle_free_bo(struct protogpu_file *pf, unsigned long arg)
{
	struct protogpu_ioctl_free_bo req;
	struct protogpu_ctx *ctx;
	struct protogpu_bo *bo;

	if (copy_from_user(&req, (void __user *)arg, sizeof(req)))
		return -EFAULT;

	mutex_lock(&pf->lock);
	ctx = find_ctx_locked(pf, req.ctx_id);
	if (!ctx) {
		mutex_unlock(&pf->lock);
		return -ENOENT;
	}

	bo = find_bo_locked(ctx, req.handle);
	if (!bo) {
		mutex_unlock(&pf->lock);
		return -ENOENT;
	}

	list_del(&bo->link);
	mutex_unlock(&pf->lock);
	vfree(bo->backing);
	kfree(bo);
	return 0;
}

static long handle_submit_job(struct protogpu_file *pf, unsigned long arg)
{
	struct protogpu_ioctl_submit_job req;
	struct protogpu_ctx *ctx;
	struct protogpu_job *job;
	struct protogpu_buffer_binding *bindings = NULL;
	struct protogpu_bo **bound_bos = NULL;
	void *arena = NULL;
	char broker_sock[PROTOGPU_IOCTL_FD_PATH_MAX];
	u64 bindings_bytes;
	u64 i;
	int rc;

	if (copy_from_user(&req, (void __user *)arg, sizeof(req)))
		return -EFAULT;
	if (req.job.uapi_version != PROTOGPU_UAPI_VERSION_V1)
		return -EINVAL;
	if (req.arena_bytes > 0 && req.arena_user_ptr == 0)
		return -EINVAL;

	if (req.arena_bytes) {
		if (req.arena_bytes > (16ull * 1024ull * 1024ull))
			return -E2BIG;
		arena = kmalloc(req.arena_bytes, GFP_KERNEL);
		if (!arena)
			return -ENOMEM;
		if (copy_from_user(arena, (void __user *)(uintptr_t)req.arena_user_ptr, req.arena_bytes)) {
			kfree(arena);
			return -EFAULT;
		}
	}

	bindings_bytes = (u64)req.job.buffer_bindings.count * (u64)sizeof(*bindings);
	if (!checked_span(req.job.buffer_bindings.offset, bindings_bytes, req.arena_bytes)) {
		kfree(arena);
		return -EINVAL;
	}
	if (req.job.buffer_bindings.count != 0)
		bindings = (struct protogpu_buffer_binding *)((u8 *)arena + req.job.buffer_bindings.offset);

	bound_bos = kcalloc(req.job.buffer_bindings.count, sizeof(*bound_bos), GFP_KERNEL);
	if (req.job.buffer_bindings.count != 0 && !bound_bos) {
		kfree(arena);
		return -ENOMEM;
	}

	job = kzalloc(sizeof(*job), GFP_KERNEL);
	if (!job) {
		kfree(arena);
		return -ENOMEM;
	}
	init_completion(&job->done);

	mutex_lock(&pf->lock);
	ctx = find_ctx_locked(pf, req.ctx_id);
	if (!ctx) {
		mutex_unlock(&pf->lock);
		kfree(job);
		kfree(arena);
		return -ENOENT;
	}

	for (i = 0; i < req.job.buffer_bindings.count; i++) {
		bound_bos[i] = find_bo_locked(ctx, bindings[i].handle);
		if (!bound_bos[i]) {
			mutex_unlock(&pf->lock);
			kfree(job);
			kfree(bound_bos);
			kfree(arena);
			return -ENOENT;
		}
	}

	if (req.job_id == 0)
		req.job_id = ctx->next_job_id++;
	job->id = req.job_id;

	if (find_job_locked(ctx, job->id)) {
		mutex_unlock(&pf->lock);
		kfree(job);
		kfree(bound_bos);
		kfree(arena);
		return -EEXIST;
	}

	strscpy(broker_sock, pf->broker_sock, sizeof(broker_sock));
	rc = submit_to_broker_locked(broker_sock, &req, arena, bindings, bound_bos, job);
	if (rc) {
		mutex_unlock(&pf->lock);
		kfree(job);
		kfree(bound_bos);
		kfree(arena);
		return rc;
	}

	list_add_tail(&job->link, &ctx->job_list);
	complete_all(&job->done);
	mutex_unlock(&pf->lock);
	kfree(bound_bos);
	kfree(arena);

	if (copy_to_user((void __user *)arg, &req, sizeof(req)))
		return -EFAULT;
	return 0;
}

static int protogpu_mmap(struct file *file, struct vm_area_struct *vma)
{
	struct protogpu_file *pf = file->private_data;
	struct protogpu_bo *bo;
	u64 req_offset = ((u64)vma->vm_pgoff) << PAGE_SHIFT;
	u64 req_bytes = (u64)(vma->vm_end - vma->vm_start);
	int rc;

	mutex_lock(&pf->lock);
	bo = find_bo_by_offset_locked(pf, req_offset);
	if (!bo) {
		mutex_unlock(&pf->lock);
		return -ENOENT;
	}
	if (req_bytes > PAGE_ALIGN(bo->bytes)) {
		mutex_unlock(&pf->lock);
		return -EINVAL;
	}

	vm_flags_set(vma, VM_DONTDUMP);
	rc = remap_vmalloc_range(vma, bo->backing, 0);
	mutex_unlock(&pf->lock);
	return rc;
}

static long handle_wait_job(struct protogpu_file *pf, unsigned long arg)
{
	struct protogpu_ioctl_wait_job req;
	struct protogpu_ctx *ctx;
	struct protogpu_job *job;

	if (copy_from_user(&req, (void __user *)arg, sizeof(req)))
		return -EFAULT;

	mutex_lock(&pf->lock);
	ctx = find_ctx_locked(pf, req.ctx_id);
	if (!ctx) {
		mutex_unlock(&pf->lock);
		return -ENOENT;
	}
	job = find_job_locked(ctx, req.job_id);
	if (!job) {
		mutex_unlock(&pf->lock);
		return -ENOENT;
	}
	req.status = job->status;
	req.steps = job->steps;
	mutex_unlock(&pf->lock);

	if (copy_to_user((void __user *)arg, &req, sizeof(req)))
		return -EFAULT;
	return 0;
}

static long handle_query_job(struct protogpu_file *pf, unsigned long arg)
{
	struct protogpu_ioctl_query_job req;
	struct protogpu_ctx *ctx;
	struct protogpu_job *job;

	if (copy_from_user(&req, (void __user *)arg, sizeof(req)))
		return -EFAULT;

	mutex_lock(&pf->lock);
	ctx = find_ctx_locked(pf, req.ctx_id);
	if (!ctx) {
		mutex_unlock(&pf->lock);
		return -ENOENT;
	}
	job = find_job_locked(ctx, req.job_id);
	if (!job) {
		mutex_unlock(&pf->lock);
		return -ENOENT;
	}
	req.status = job->status;
	req.steps = job->steps;
	req.diagnostic_bytes = job->diag_len;
	req.trace_bytes = 0;
	req.stats_bytes = 0;
	mutex_unlock(&pf->lock);

	if (copy_to_user((void __user *)arg, &req, sizeof(req)))
		return -EFAULT;
	return 0;
}

static long handle_read_diag(struct protogpu_file *pf, unsigned long arg)
{
	struct protogpu_ioctl_read_diag req;
	struct protogpu_ctx *ctx;
	struct protogpu_job *job;
	size_t to_copy;

	if (copy_from_user(&req, (void __user *)arg, sizeof(req)))
		return -EFAULT;

	mutex_lock(&pf->lock);
	ctx = find_ctx_locked(pf, req.ctx_id);
	if (!ctx) {
		mutex_unlock(&pf->lock);
		return -ENOENT;
	}
	job = find_job_locked(ctx, req.job_id);
	if (!job) {
		mutex_unlock(&pf->lock);
		return -ENOENT;
	}

	to_copy = min_t(size_t, req.capacity, job->diag_len);
	if (to_copy && copy_to_user((void __user *)(uintptr_t)req.user_ptr, job->diag, to_copy)) {
		mutex_unlock(&pf->lock);
		return -EFAULT;
	}
	req.bytes_written = to_copy;
	mutex_unlock(&pf->lock);

	if (copy_to_user((void __user *)arg, &req, sizeof(req)))
		return -EFAULT;
	return 0;
}

static long protogpu_unlocked_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	struct protogpu_file *pf = file->private_data;

	switch (cmd) {
	case PROTOGPU_IOCTL_CREATE_CTX:
		return handle_create_ctx(pf, arg);
	case PROTOGPU_IOCTL_DESTROY_CTX:
		return handle_destroy_ctx(pf, arg);
	case PROTOGPU_IOCTL_SET_BROKER:
		return handle_set_broker(pf, arg);
	case PROTOGPU_IOCTL_ALLOC_BO:
		return handle_alloc_bo(pf, arg);
	case PROTOGPU_IOCTL_FREE_BO:
		return handle_free_bo(pf, arg);
	case PROTOGPU_IOCTL_SUBMIT_JOB:
		return handle_submit_job(pf, arg);
	case PROTOGPU_IOCTL_WAIT_JOB:
		return handle_wait_job(pf, arg);
	case PROTOGPU_IOCTL_QUERY_JOB:
		return handle_query_job(pf, arg);
	case PROTOGPU_IOCTL_READ_DIAG:
		return handle_read_diag(pf, arg);
	default:
		return -ENOTTY;
	}
}

static int protogpu_open(struct inode *inode, struct file *file)
{
	struct protogpu_file *pf;

	pf = kzalloc(sizeof(*pf), GFP_KERNEL);
	if (!pf)
		return -ENOMEM;

	mutex_init(&pf->lock);
	pf->next_ctx_id = 1;
	INIT_LIST_HEAD(&pf->ctx_list);
	pf->broker_sock[0] = '\0';

	file->private_data = pf;
	return 0;
}

static int protogpu_release(struct inode *inode, struct file *file)
{
	struct protogpu_file *pf = file->private_data;
	struct protogpu_ctx *ctx, *tmp;

	if (!pf)
		return 0;

	mutex_lock(&pf->lock);
	list_for_each_entry_safe(ctx, tmp, &pf->ctx_list, link) {
		list_del(&ctx->link);
		free_ctx(ctx);
	}
	mutex_unlock(&pf->lock);

	kfree(pf);
	return 0;
}

static const struct file_operations protogpu_fops = {
	.owner = THIS_MODULE,
	.open = protogpu_open,
	.release = protogpu_release,
	.unlocked_ioctl = protogpu_unlocked_ioctl,
	.mmap = protogpu_mmap,
#ifdef CONFIG_COMPAT
	.compat_ioctl = protogpu_unlocked_ioctl,
#endif
};

static struct miscdevice protogpu_miscdev = {
	.minor = MISC_DYNAMIC_MINOR,
	.name = "protogpu0",
	.fops = &protogpu_fops,
	.mode = 0666,
};

static int __init protogpu_init(void)
{
	pr_info("protogpu: loading skeleton module\n");
	return misc_register(&protogpu_miscdev);
}

static void __exit protogpu_exit(void)
{
	misc_deregister(&protogpu_miscdev);
	pr_info("protogpu: unloaded skeleton module\n");
}

module_init(protogpu_init);
module_exit(protogpu_exit);

MODULE_AUTHOR("ProtoGPU virtual HW");
MODULE_DESCRIPTION("ProtoGPU virtual hardware driver skeleton");
MODULE_LICENSE("GPL");
