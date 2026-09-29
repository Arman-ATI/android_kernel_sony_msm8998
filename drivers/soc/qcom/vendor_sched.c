// SPDX-License-Identifier: GPL-2.0-only
/*
 * Compatibility front end for the Pixel /proc/vendor_sched interface.
 *
 * Pixel drives its scheduler through /proc/vendor_sched, which its vh_sched
 * module provides on top of scheduler vendor hooks this tree does not have.
 * Provide the subset of nodes Pixel userspace writes and translate each write
 * onto an existing cpu cgroup file: cpu.uclamp.min, cpu.uclamp.max and
 * cpu.uclamp.latency_sensitive, and cgroup.procs and tasks for group nodes.
 *
 * Copyright (c) 2026, Edrick Sinsuan <evcsinsuan@gmail.com>
 */

#include <linux/capability.h>
#include <linux/cgroup.h>
#include <linux/cred.h>
#include <linux/fs.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/proc_fs.h>
#include <linux/sched.h>
#include <linux/sched/sysctl.h>
#include <linux/sched/task.h>
#include <linux/slab.h>
#include <linux/stat.h>
#include <linux/string.h>
#include <linux/uidgid.h>
#include <linux/uaccess.h>

#define VS_CPUCTL_ROOT		"/dev/cpuctl"
#define VS_PROC_DIR		"vendor_sched"
#define VS_SCHED_QOS_DIR	"sched_qos"
#define VS_MAX_VALUE		32
#define VS_MAX_NAME		64
#define VS_MAX_PATH		128
#define VS_RAMPUP_MAX		16

/*
 * cpu.uclamp.min and cpu.uclamp.max take a percentage in fixed point with
 * VS_PERCENT_DIGITS decimals, where VS_PERCENT_SCALE is 100 percent. The Pixel
 * interface carries uclamp on the 0..SCHED_CAPACITY_SCALE scale instead, so a
 * write is converted at the resolution the file itself has.
 */
#define VS_PERCENT		100
#define VS_PERCENT_DIGITS	2
#define VS_PERCENT_STEP		100
#define VS_PERCENT_SCALE	(VS_PERCENT * VS_PERCENT_STEP)

/* Value nodes are readable, action nodes are write-only, as in Pixel. */
#define VS_MODE_RW		0644
#define VS_MODE_WO		0200

/* A group index of -1 stands for the nodes that are not per group. */
#define VS_GLOBAL_GROUP		(-1)

/*
 * Pixel group names and the cpuctl group each maps to on this platform. Groups
 * with no cpuctl directory of their own are folded into the closest group that
 * exists, and the mapping stays explicit so it is obvious which are real.
 */
static const struct vs_group {
	const char *name;
	const char *cpuctl;
} vs_groups[] = {
	{ "ta",		"top-app" },
	{ "fg",		"foreground" },
	{ "sys",	"system" },
	{ "bg",		"background" },
	{ "sysbg",	"system-background" },
	{ "rt",		"rt" },
	{ "dex2oat",	"dex2oat" },
	{ "sf",		"foreground" },
	{ "cam",	"camera-daemon" },
	{ "cam_power",	"foreground" },
	{ "nnapi",	"nnapi-hal" },
	{ "ota",	"background" },
};

enum vs_kind {
	VS_UCLAMP_MIN,
	VS_UCLAMP_MAX,
	VS_PREFER_IDLE,
	VS_SET_TASKS,
	VS_SET_PROCS,
	VS_FORK_RESET_SET,
	VS_FORK_RESET_CLEAR,
	VS_TGID_TYPE,
	VS_RAMPUP_SET,
	VS_RAMPUP_CLEAR,
};

/* One node: a name format for the per group nodes, a literal one otherwise. */
struct vs_desc {
	const char *name;
	enum vs_kind kind;
};

struct vs_node {
	const char *name;
	enum vs_kind kind;
	int group;		/* index into vs_groups */
	struct mutex lock;
	char value[VS_MAX_VALUE];
};

static struct vs_node *vs_nodes;
static unsigned int vs_node_count;
static unsigned int vs_node_max;
static struct proc_dir_entry *vs_root;

/*
 * check_tgid_type reports its answer through the write return value: 1 for
 * SystemUI, 2 for Chrome, otherwise the default. Pixel decides this inside
 * vendor_sched; here it is decided from the task name.
 */
#define VS_TAG_SYSTEM_UI	1
#define VS_TAG_CHROME		2

/* The default answer must not collide with the SYSTEM_UI/CHROME tags. */
#define VS_TAG_NONE		0

static int vs_tgid_tag(unsigned long tgid, int fallback)
{
	struct task_struct *task;
	char comm[TASK_COMM_LEN];

	task = find_get_task_by_vpid(tgid);
	if (!task)
		return fallback;

	get_task_comm(comm, task);
	put_task_struct(task);

	if (strnstr(comm, "systemui", TASK_COMM_LEN))
		return VS_TAG_SYSTEM_UI;
	if (strnstr(comm, "chrome", TASK_COMM_LEN) ||
	    strnstr(comm, "CrBrowserMain", TASK_COMM_LEN) ||
	    strnstr(comm, "CrRendererMain", TASK_COMM_LEN))
		return VS_TAG_CHROME;

	return fallback;
}

/*
 * kernfs parses the same buffer userspace would have written, so the cgroup
 * validation and percent handling run as for a direct write.
 */
static int vs_write_file(const char *path, const char *buf, size_t count)
{
	struct file *file;
	ssize_t written;

	file = filp_open(path, O_WRONLY, 0);
	if (IS_ERR(file))
		return PTR_ERR(file);

	written = kernel_write(file, buf, count, &file->f_pos);
	filp_close(file, NULL);

	if (written < 0)
		return written;

	return (int)count;
}

/*
 * Reject writes targeting another task unless the caller is root, shares the
 * target's euid/suid, or holds CAP_SYS_NICE in its user namespace. The power
 * HAL runs as root, so this is behaviour-neutral for it.
 */
static int vs_check_cred(struct task_struct *p)
{
	const struct cred *cred = current_cred();
	const struct cred *tcred = get_task_cred(p);
	bool allowed;

	allowed = uid_eq(cred->euid, GLOBAL_ROOT_UID) ||
		  uid_eq(cred->euid, tcred->uid) ||
		  uid_eq(cred->euid, tcred->suid) ||
		  ns_capable(tcred->user_ns, CAP_SYS_NICE);
	put_cred(tcred);

	return allowed ? 0 : -EACCES;
}

static int vs_write_attr(int group, const char *attr, const char *buf,
			 size_t count)
{
	char path[VS_MAX_PATH];

	snprintf(path, sizeof(path), "%s/%s/cpu.uclamp.%s", VS_CPUCTL_ROOT,
		 vs_groups[group].cpuctl, attr);

	return vs_write_file(path, buf, count);
}

static int vs_write_pid(int group, const char *file, char *buf)
{
	struct task_struct *task;
	char path[VS_MAX_PATH];
	char pid[VS_MAX_VALUE];
	unsigned long val;
	int ret;

	ret = kstrtoul(strim(buf), 0, &val);
	if (ret || !val)
		return -EINVAL;

	task = find_get_task_by_vpid((pid_t)val);
	if (!task)
		return -ESRCH;

	ret = vs_check_cred(task);
	put_task_struct(task);
	if (ret)
		return ret;

	snprintf(pid, sizeof(pid), "%lu", val);
	snprintf(path, sizeof(path), "%s/%s/%s", VS_CPUCTL_ROOT,
		 vs_groups[group].cpuctl, file);

	return vs_write_file(path, pid, strlen(pid));
}

/* A uclamp write is a raw capacity value on the 0..SCHED_CAPACITY_SCALE scale. */
static int vs_uclamp_value(char *buf, unsigned long *value)
{
	int val;

	if (kstrtoint(strim(buf), 0, &val))
		return -EINVAL;
	if (val < 0 || val > SCHED_CAPACITY_SCALE)
		return -EINVAL;

	*value = val;

	return 0;
}

/* A fork reset write takes a pid: -EINVAL if malformed, -ESRCH if unknown. */
static int vs_fork_reset(char *buf)
{
	struct task_struct *task;
	unsigned long pid;
	int ret;

	if (kstrtoul(strim(buf), 0, &pid) || !pid)
		return -EINVAL;

	task = find_get_task_by_vpid((pid_t)pid);
	if (!task)
		return -ESRCH;

	ret = vs_check_cred(task);
	put_task_struct(task);

	return ret;
}

/*
 * A rampup set write is "<tid>:<multiplier>", as Pixel's power HAL writes it,
 * and it applies to that one task. Values above VS_RAMPUP_MAX are rejected;
 * the EWMA shift they select must stay far from overflow.
 */
static int vs_rampup_set(char *buf)
{
	struct task_struct *task;
	unsigned long multiplier, pid;
	char *str = strim(buf);
	char *sep = strchr(str, ':');
	char saved;

	if (!sep)
		return -EINVAL;

	saved = *sep;
	*sep = '\0';

	if (kstrtoul(str, 10, &pid) || !pid)
		goto err;
	if (kstrtoul(strim(sep + 1), 10, &multiplier) ||
	    multiplier > VS_RAMPUP_MAX)
		goto err;

	*sep = saved;

	task = find_get_task_by_vpid((pid_t)pid);
	if (!task)
		return -ESRCH;

	WRITE_ONCE(task->rampup_multiplier, (unsigned int)multiplier);
	WRITE_ONCE(task->rampup_multiplier_set, 1);
	put_task_struct(task);

	return 0;

err:
	*sep = saved;
	return -EINVAL;
}

/* A rampup clear write is a bare tid and restores the default multiplier. */
static int vs_rampup_clear(char *buf)
{
	struct task_struct *task;
	unsigned long pid;

	if (kstrtoul(strim(buf), 10, &pid) || !pid)
		return -EINVAL;

	task = find_get_task_by_vpid((pid_t)pid);
	if (!task)
		return -ESRCH;

	WRITE_ONCE(task->rampup_multiplier, sysctl_sched_rampup_multiplier);
	WRITE_ONCE(task->rampup_multiplier_set, 0);
	put_task_struct(task);

	return 0;
}

/* The percentage equivalent at the cgroup file's own resolution. */
static void vs_uclamp_percent(unsigned long util, char *out, size_t len)
{
	unsigned long fixed = DIV_ROUND_CLOSEST_ULL((u64)util * VS_PERCENT_SCALE,
						    SCHED_CAPACITY_SCALE);

	snprintf(out, len, "%lu.%0*lu", fixed / VS_PERCENT_STEP,
		 VS_PERCENT_DIGITS, fixed % VS_PERCENT_STEP);
}

static const char *vs_uclamp_attr(enum vs_kind kind)
{
	return kind == VS_UCLAMP_MIN ? "min" : "max";
}

/*
 * Feed the formatted percentage back through the kernel's parser for the file
 * and the arithmetic its writer applies; the init check sweeps the whole scale,
 * so a change in either shows up here and no value has to be named.
 */
static unsigned long vs_percent_util(char *percent)
{
	s64 fixed = 0;

	if (cgroup_parse_float(percent, VS_PERCENT_DIGITS, &fixed))
		return SCHED_CAPACITY_SCALE + 1;
	if (fixed < 0 || (u64)fixed > VS_PERCENT_SCALE)
		return SCHED_CAPACITY_SCALE + 1;

	return DIV_ROUND_CLOSEST_ULL((u64)fixed * SCHED_CAPACITY_SCALE,
				     VS_PERCENT_SCALE);
}

static void vs_uclamp_check(void)
{
	unsigned long util;

	for (util = 0; util <= SCHED_CAPACITY_SCALE; util++) {
		char percent[VS_MAX_VALUE];

		vs_uclamp_percent(util, percent, sizeof(percent));
		WARN_ONCE(vs_percent_util(percent) != util,
			  "vendor_sched: uclamp %lu does not survive %s\n",
			  util, percent);
	}
}

/*
 * Carry out one node write and return the value the node reports on readback.
 * check_tgid_type is routed out earlier because its answer is the write return
 * value itself.
 */
static int vs_node_apply(struct vs_node *node, char *buf, char *value,
			 size_t len)
{
	const char *stored;
	int ret;

	switch (node->kind) {
	case VS_UCLAMP_MIN:
	case VS_UCLAMP_MAX: {
		unsigned long util;
		char conv[VS_MAX_VALUE];

		ret = vs_uclamp_value(buf, &util);
		if (ret)
			return ret;

		/* The node reports the capacity value it was given. */
		stored = buf;
		vs_uclamp_percent(util, conv, sizeof(conv));
		ret = vs_write_attr(node->group, vs_uclamp_attr(node->kind),
				    conv, strlen(conv));
		break;
	}
	case VS_PREFER_IDLE: {
		bool idle;

		if (kstrtobool(strim(buf), &idle))
			return -EINVAL;

		/* cpu.uclamp.latency_sensitive takes that flag as 0 or 1. */
		stored = idle ? "true" : "false";
		ret = vs_write_attr(node->group, "latency_sensitive",
				    idle ? "1" : "0", 1);
		break;
	}
	case VS_SET_TASKS:
		stored = buf;
		ret = vs_write_pid(node->group, "tasks", buf);
		break;
	case VS_SET_PROCS:
		stored = buf;
		ret = vs_write_pid(node->group, "cgroup.procs", buf);
		break;
	case VS_FORK_RESET_SET:
	case VS_FORK_RESET_CLEAR:
		ret = vs_fork_reset(buf);
		if (ret < 0)
			return ret;

		/* No fork hook here, so the pid is only validated. */
		stored = buf;
		ret = 0;
		break;
	case VS_RAMPUP_SET:
		ret = vs_rampup_set(buf);
		if (ret < 0)
			return ret;

		/* The node reports the tid:value pair it was given. */
		stored = buf;
		break;
	case VS_RAMPUP_CLEAR:
		ret = vs_rampup_clear(buf);
		if (ret < 0)
			return ret;

		stored = buf;
		break;
	default:
		return -EINVAL;
	}

	if (ret < 0)
		return ret;

	strscpy(value, stored, len);
	strim(value);

	return 0;
}

static ssize_t vs_node_read(struct file *file, char __user *ubuf,
			    size_t count, loff_t *ppos)
{
	struct vs_node *node = PDE_DATA(file_inode(file));
	size_t len;

	if (!node)
		return -EINVAL;

	mutex_lock(&node->lock);
	len = strlen(node->value);
	mutex_unlock(&node->lock);

	return simple_read_from_buffer(ubuf, count, ppos, node->value, len);
}

static ssize_t vs_node_write(struct file *file, const char __user *ubuf,
			     size_t count, loff_t *ppos)
{
	struct vs_node *node = PDE_DATA(file_inode(file));
	char buf[VS_MAX_VALUE];
	char value[VS_MAX_VALUE];
	size_t written = count;
	int ret;

	if (!node || !count)
		return -EINVAL;

	if (count >= sizeof(buf))
		count = sizeof(buf) - 1;

	if (copy_from_user(buf, ubuf, count))
		return -EFAULT;
	buf[count] = '\0';

	if (node->kind == VS_TGID_TYPE) {
		unsigned long tgid;

		if (kstrtoul(strim(buf), 10, &tgid))
			return -EINVAL;

		return vs_tgid_tag(tgid, VS_TAG_NONE);
	}

	ret = vs_node_apply(node, buf, value, sizeof(value));
	if (ret < 0)
		return ret;

	mutex_lock(&node->lock);
	strscpy(node->value, value, sizeof(node->value));
	mutex_unlock(&node->lock);

	/*
	 * Report the bytes consumed, not the converted length: callers that
	 * subtract the return value from the remaining count would underflow.
	 */
	return written;
}

static const struct file_operations vs_node_ops = {
	.read	= vs_node_read,
	.write	= vs_node_write,
	.llseek	= default_llseek,
};

static umode_t vs_node_mode(enum vs_kind kind)
{
	switch (kind) {
	case VS_UCLAMP_MIN:
	case VS_UCLAMP_MAX:
	case VS_PREFER_IDLE:
		return VS_MODE_RW;
	default:
		return VS_MODE_WO;
	}
}

static void vs_add_node(struct proc_dir_entry *parent, const char *name,
			enum vs_kind kind, int group)
{
	struct vs_node *node = &vs_nodes[vs_node_count];
	struct proc_dir_entry *entry;

	if (!parent)
		return;

	if (vs_node_count >= vs_node_max) {
		WARN_ONCE(1, "vendor_sched: no room for %s\n", name);
		return;
	}

	node->name = kstrdup(name, GFP_KERNEL);
	if (!node->name)
		return;

	node->kind = kind;
	node->group = group;
	mutex_init(&node->lock);
	strscpy(node->value, "0", sizeof(node->value));

	entry = proc_create_data(node->name, vs_node_mode(kind), parent,
				 &vs_node_ops, node);
	if (!entry)
		goto err_free_name;

	vs_node_count++;
	return;

err_free_name:
	kfree(node->name);
	node->name = NULL;
}

static const struct vs_desc vs_group_nodes[] = {
	{ "%s_uclamp_min",	VS_UCLAMP_MIN },
	{ "%s_uclamp_max",	VS_UCLAMP_MAX },
	{ "%s_prefer_idle",	VS_PREFER_IDLE },
	{ "set_task_group_%s",	VS_SET_TASKS },
	{ "set_proc_group_%s",	VS_SET_PROCS },
};

static const struct vs_desc vs_global_nodes[] = {
	{ "uclamp_fork_reset_set",	VS_FORK_RESET_SET },
	{ "uclamp_fork_reset_clear",	VS_FORK_RESET_CLEAR },
	{ "check_tgid_type",		VS_TGID_TYPE },
};

/* Nodes the power HAL opens directly, outside powerhint.json. */
static const struct vs_desc vs_sched_qos_nodes[] = {
	{ "rampup_multiplier_set",	VS_RAMPUP_SET },
	{ "rampup_multiplier_clear",	VS_RAMPUP_CLEAR },
};

static void vs_add_nodes(struct proc_dir_entry *parent,
			 const struct vs_desc *descs, unsigned int count)
{
	unsigned int i;

	for (i = 0; i < count; i++)
		vs_add_node(parent, descs[i].name, descs[i].kind,
			    VS_GLOBAL_GROUP);
}

static void vs_add_group_nodes(struct proc_dir_entry *parent, const char *group,
			       int index)
{
	char name[VS_MAX_NAME];
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(vs_group_nodes); i++) {
		snprintf(name, sizeof(name), vs_group_nodes[i].name, group);
		vs_add_node(parent, name, vs_group_nodes[i].kind, index);
	}
}

static int __init vs_init(void)
{
	struct proc_dir_entry *parent;
	unsigned int i;
	int ret = -ENOMEM;

	vs_node_max = ARRAY_SIZE(vs_groups) * ARRAY_SIZE(vs_group_nodes) +
		      ARRAY_SIZE(vs_global_nodes) +
		      ARRAY_SIZE(vs_sched_qos_nodes);

	vs_uclamp_check();

	vs_nodes = kcalloc(vs_node_max, sizeof(*vs_nodes), GFP_KERNEL);
	if (!vs_nodes)
		return ret;

	vs_root = proc_mkdir(VS_PROC_DIR, NULL);
	if (!vs_root)
		goto err_free_nodes;

	for (i = 0; i < ARRAY_SIZE(vs_groups); i++)
		vs_add_group_nodes(vs_root, vs_groups[i].name, i);

	vs_add_nodes(vs_root, vs_global_nodes, ARRAY_SIZE(vs_global_nodes));

	parent = proc_mkdir(VS_SCHED_QOS_DIR, vs_root);
	if (parent)
		vs_add_nodes(parent, vs_sched_qos_nodes,
			     ARRAY_SIZE(vs_sched_qos_nodes));

	pr_info("vendor_sched: %u compatibility nodes created\n", vs_node_count);
	return 0;

err_free_nodes:
	kfree(vs_nodes);
	vs_nodes = NULL;
	return ret;
}

static void __exit vs_exit(void)
{
	unsigned int i;

	proc_remove(vs_root);
	vs_root = NULL;

	for (i = 0; i < vs_node_count; i++)
		kfree(vs_nodes[i].name);

	kfree(vs_nodes);
	vs_nodes = NULL;
	vs_node_count = 0;
}

module_init(vs_init);
module_exit(vs_exit);

MODULE_DESCRIPTION("Pixel /proc/vendor_sched compatibility front end");
MODULE_LICENSE("GPL");
