// SPDX-License-Identifier: GPL-2.0-or-later
#include <fcntl.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <linux/fs.h>
#include <inttypes.h>
#include <asm/byteorder.h>
#include <sys/sysinfo.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <dirent.h>
#include <time.h>

#include "common.h"
#include "nvme.h"
#include "libnvme.h"
#include "plugin.h"
#include "linux/types.h"
#include "nvme-wrap.h"
#include "nvme-print.h"
#include "util/cleanup.h"

#define CREATE_CMD
#include "sfx-nvme.h"
#include "sfx-types.h"

#define SFX_PAGE_SHIFT						12
#define SECTOR_SHIFT						9

#define SFX_GET_FREESPACE			_IOWR('N', 0x240, struct sfx_freespace_ctx)
#define NVME_IOCTL_CLR_CARD			_IO('N', 0x47)

#define IDEMA_CAP(exp_GB)			(((__u64)exp_GB - 50ULL) * 1953504ULL + 97696368ULL)
#define IDEMA_CAP2GB(exp_sector)		(((__u64)exp_sector - 97696368ULL) / 1953504ULL + 50ULL)

#define VANDA_MAJOR_IDX		0
#define VANDA_MINOR_IDX		0

#define MYRTLE_MAJOR_IDX        4
#define MYRTLE_MINOR_IDX        1


int nvme_query_cap(int fd, __u32 nsid, __u32 data_len, void *data)
{
	int rc = 0;
	struct nvme_passthru_cmd cmd = {
		.opcode		= nvme_admin_query_cap_info,
		.nsid		= nsid,
		.addr		= (__u64)(uintptr_t) data,
		.data_len	= data_len,
	};

	rc = ioctl(fd, SFX_GET_FREESPACE, data);
	return rc ? nvme_submit_admin_passthru(fd, &cmd, NULL) : 0;
}

int nvme_change_cap(int fd, __u32 nsid, __u64 capacity)
{
	struct nvme_passthru_cmd cmd = {
		.opcode	= nvme_admin_change_cap,
		.nsid	= nsid,
		.cdw10	= (capacity & 0xffffffff),
		.cdw11	= (capacity >> 32),
	};

	return nvme_submit_admin_passthru(fd, &cmd, NULL);
}

int nvme_sfx_set_features(int fd, __u32 nsid, __u32 fid, __u32 value)
{
	struct nvme_passthru_cmd cmd = {
		.opcode	= nvme_admin_sfx_set_features,
		.nsid	= nsid,
		.cdw10	= fid,
		.cdw11	= value,
	};

	return nvme_submit_admin_passthru(fd, &cmd, NULL);
}

int nvme_sfx_get_features(int fd, __u32 nsid, __u32 fid, __u32 *result)
{
	int err = 0;
		struct nvme_passthru_cmd cmd = {
		.opcode	= nvme_admin_sfx_get_features,
		.nsid	= nsid,
		.cdw10	= fid,
	};

	err = nvme_submit_admin_passthru(fd, &cmd, NULL);
	if (!err && result)
		*result = cmd.result;

	return err;
}

static void show_sfx_smart_log_jsn(struct nvme_additional_smart_log *smart,
		unsigned int nsid, const char *devname)
{
	struct json_object *root, *entry_stats, *dev_stats, *multi;

	root = json_create_object();
	json_object_add_value_string(root, "ScaleFlux Smart log", devname);

	dev_stats = json_create_object();

	entry_stats = json_create_object();
	json_object_add_value_int(entry_stats, "normalized", smart->program_fail_cnt.norm);
	json_object_add_value_int(entry_stats, "raw", int48_to_long(smart->program_fail_cnt.raw));
	json_object_add_value_object(dev_stats, "program_fail_count", entry_stats);

	entry_stats = json_create_object();
	json_object_add_value_int(entry_stats, "normalized", smart->erase_fail_cnt.norm);
	json_object_add_value_int(entry_stats, "raw", int48_to_long(smart->erase_fail_cnt.raw));
	json_object_add_value_object(dev_stats, "erase_fail_count", entry_stats);

	entry_stats = json_create_object();
	json_object_add_value_int(entry_stats, "normalized", smart->wear_leveling_cnt.norm);
	multi = json_create_object();
	json_object_add_value_int(multi, "min", le16_to_cpu(smart->wear_leveling_cnt.wear_level.min));
	json_object_add_value_int(multi, "max", le16_to_cpu(smart->wear_leveling_cnt.wear_level.max));
	json_object_add_value_int(multi, "avg", le16_to_cpu(smart->wear_leveling_cnt.wear_level.avg));
	json_object_add_value_object(entry_stats, "raw", multi);
	json_object_add_value_object(dev_stats, "wear_leveling", entry_stats);

	entry_stats = json_create_object();
	json_object_add_value_int(entry_stats, "normalized", smart->e2e_err_cnt.norm);
	json_object_add_value_int(entry_stats, "raw", int48_to_long(smart->e2e_err_cnt.raw));
	json_object_add_value_object(dev_stats, "end_to_end_error_detection_count", entry_stats);

	entry_stats = json_create_object();
	json_object_add_value_int(entry_stats, "normalized", smart->crc_err_cnt.norm);
	json_object_add_value_int(entry_stats, "raw", int48_to_long(smart->crc_err_cnt.raw));
	json_object_add_value_object(dev_stats, "crc_error_count", entry_stats);

	entry_stats = json_create_object();
	json_object_add_value_int(entry_stats, "normalized", smart->timed_workload_media_wear.norm);
	json_object_add_value_float(entry_stats, "raw", ((float)int48_to_long(smart->timed_workload_media_wear.raw)) / 1024);
	json_object_add_value_object(dev_stats, "timed_workload_media_wear", entry_stats);

	entry_stats = json_create_object();
	json_object_add_value_int(entry_stats, "normalized", smart->timed_workload_host_reads.norm);
	json_object_add_value_int(entry_stats, "raw", int48_to_long(smart->timed_workload_host_reads.raw));
	json_object_add_value_object(dev_stats, "timed_workload_host_reads", entry_stats);

	entry_stats = json_create_object();
	json_object_add_value_int(entry_stats, "normalized", smart->timed_workload_timer.norm);
	json_object_add_value_int(entry_stats, "raw", int48_to_long(smart->timed_workload_timer.raw));
	json_object_add_value_object(dev_stats, "timed_workload_timer", entry_stats);

	entry_stats = json_create_object();
	json_object_add_value_int(entry_stats, "normalized", smart->thermal_throttle_status.norm);
	multi = json_create_object();
	json_object_add_value_int(multi, "pct", smart->thermal_throttle_status.thermal_throttle.pct);
	json_object_add_value_int(multi, "cnt", smart->thermal_throttle_status.thermal_throttle.count);
	json_object_add_value_object(entry_stats, "raw", multi);
	json_object_add_value_object(dev_stats, "thermal_throttle_status", entry_stats);

	entry_stats = json_create_object();
	json_object_add_value_int(entry_stats, "normalized", smart->retry_buffer_overflow_cnt.norm);
	json_object_add_value_int(entry_stats, "raw",	  int48_to_long(smart->retry_buffer_overflow_cnt.raw));
	json_object_add_value_object(dev_stats, "retry_buffer_overflow_count", entry_stats);

	entry_stats = json_create_object();
	json_object_add_value_int(entry_stats, "normalized", smart->pll_lock_loss_cnt.norm);
	json_object_add_value_int(entry_stats, "raw",	  int48_to_long(smart->pll_lock_loss_cnt.raw));
	json_object_add_value_object(dev_stats, "pll_lock_loss_count", entry_stats);

	entry_stats = json_create_object();
	json_object_add_value_int(entry_stats, "normalized", smart->nand_bytes_written.norm);
	json_object_add_value_int(entry_stats, "raw",	  int48_to_long(smart->nand_bytes_written.raw));
	json_object_add_value_object(dev_stats, "nand_bytes_written", entry_stats);

	entry_stats = json_create_object();
	json_object_add_value_int(entry_stats, "normalized", smart->host_bytes_written.norm);
	json_object_add_value_int(entry_stats, "raw",	  int48_to_long(smart->host_bytes_written.raw));
	json_object_add_value_object(dev_stats, "host_bytes_written", entry_stats);

	entry_stats = json_create_object();
	json_object_add_value_int(entry_stats, "normalized", smart->raid_recover_cnt.norm);
	json_object_add_value_int(entry_stats, "raw",	  int48_to_long(smart->raid_recover_cnt.raw));
	json_object_add_value_object(dev_stats, "raid_recover_cnt", entry_stats);

	entry_stats = json_create_object();
	json_object_add_value_int(entry_stats, "normalized", smart->prog_timeout_cnt.norm);
	json_object_add_value_int(entry_stats, "raw",	  int48_to_long(smart->prog_timeout_cnt.raw));
	json_object_add_value_object(dev_stats, "prog_timeout_cnt", entry_stats);

	entry_stats = json_create_object();
	json_object_add_value_int(entry_stats, "normalized", smart->erase_timeout_cnt.norm);
	json_object_add_value_int(entry_stats, "raw",	  int48_to_long(smart->erase_timeout_cnt.raw));
	json_object_add_value_object(dev_stats, "erase_timeout_cnt", entry_stats);

	entry_stats = json_create_object();
	json_object_add_value_int(entry_stats, "normalized", smart->read_timeout_cnt.norm);
	json_object_add_value_int(entry_stats, "raw",	  int48_to_long(smart->read_timeout_cnt.raw));
	json_object_add_value_object(dev_stats, "read_timeout_cnt", entry_stats);

	entry_stats = json_create_object();
	json_object_add_value_int(entry_stats, "normalized", smart->read_ecc_cnt.norm);
	json_object_add_value_int(entry_stats, "raw",	  int48_to_long(smart->read_ecc_cnt.raw));
	json_object_add_value_object(dev_stats, "read_ecc_cnt", entry_stats);

	entry_stats = json_create_object();
	json_object_add_value_int(entry_stats, "normalized", smart->non_media_crc_err_cnt.norm);
	json_object_add_value_int(entry_stats, "raw", int48_to_long(smart->non_media_crc_err_cnt.raw));
	json_object_add_value_object(dev_stats, "non_media_crc_err_cnt", entry_stats);

	entry_stats = json_create_object();
	json_object_add_value_int(entry_stats, "normalized", smart->compression_path_err_cnt.norm);
	json_object_add_value_int(entry_stats, "raw", int48_to_long(smart->compression_path_err_cnt.raw));
	json_object_add_value_object(dev_stats, "compression_path_err_cnt", entry_stats);

	entry_stats = json_create_object();
	json_object_add_value_int(entry_stats, "normalized", smart->out_of_space_flag.norm);
	json_object_add_value_int(entry_stats, "raw", int48_to_long(smart->out_of_space_flag.raw));
	json_object_add_value_object(dev_stats, "out_of_space_flag", entry_stats);

	entry_stats = json_create_object();
	json_object_add_value_int(entry_stats, "normalized", smart->physical_usage_ratio.norm);
	json_object_add_value_int(entry_stats, "raw", int48_to_long(smart->physical_usage_ratio.raw));
	json_object_add_value_object(dev_stats, "physical_usage_ratio", entry_stats);

	entry_stats = json_create_object();
	json_object_add_value_int(entry_stats, "normalized", smart->grown_bb.norm);
	json_object_add_value_int(entry_stats, "raw", int48_to_long(smart->grown_bb.raw));
	json_object_add_value_object(dev_stats, "grown_bb", entry_stats);

	json_object_add_value_object(root, "Device stats", dev_stats);

	json_print_object(root, NULL);
	printf("\n");
	json_free_object(root);
}

static void show_sfx_smart_log(struct nvme_additional_smart_log *smart,
		unsigned int nsid, const char *devname)
{
	printf("Additional Smart Log for ScaleFlux device:%s namespace-id:%x\n",
			devname, nsid);
	printf("key                               normalized raw\n");
	printf("program_fail_count              : %3d%%       %"PRIu64"\n",
			smart->program_fail_cnt.norm,
			int48_to_long(smart->program_fail_cnt.raw));
	printf("erase_fail_count                : %3d%%       %"PRIu64"\n",
			smart->erase_fail_cnt.norm,
			int48_to_long(smart->erase_fail_cnt.raw));
	printf("wear_leveling                   : %3d%%       min: %u, max: %u, avg: %u\n",
			smart->wear_leveling_cnt.norm,
			le16_to_cpu(smart->wear_leveling_cnt.wear_level.min),
			le16_to_cpu(smart->wear_leveling_cnt.wear_level.max),
			le16_to_cpu(smart->wear_leveling_cnt.wear_level.avg));
	printf("end_to_end_error_detection_count: %3d%%       %"PRIu64"\n",
			smart->e2e_err_cnt.norm,
			int48_to_long(smart->e2e_err_cnt.raw));
	printf("crc_error_count                 : %3d%%       %"PRIu64"\n",
			smart->crc_err_cnt.norm,
			int48_to_long(smart->crc_err_cnt.raw));
	printf("timed_workload_media_wear       : %3d%%       %.3f%%\n",
			smart->timed_workload_media_wear.norm,
			((float)int48_to_long(smart->timed_workload_media_wear.raw)) / 1024);
	printf("timed_workload_host_reads       : %3d%%       %"PRIu64"%%\n",
			smart->timed_workload_host_reads.norm,
			int48_to_long(smart->timed_workload_host_reads.raw));
	printf("timed_workload_timer            : %3d%%       %"PRIu64" min\n",
			smart->timed_workload_timer.norm,
			int48_to_long(smart->timed_workload_timer.raw));
	printf("thermal_throttle_status         : %3d%%       %u%%, cnt: %u\n",
			smart->thermal_throttle_status.norm,
			smart->thermal_throttle_status.thermal_throttle.pct,
			smart->thermal_throttle_status.thermal_throttle.count);
	printf("retry_buffer_overflow_count     : %3d%%       %"PRIu64"\n",
			smart->retry_buffer_overflow_cnt.norm,
			int48_to_long(smart->retry_buffer_overflow_cnt.raw));
	printf("pll_lock_loss_count             : %3d%%       %"PRIu64"\n",
			smart->pll_lock_loss_cnt.norm,
			int48_to_long(smart->pll_lock_loss_cnt.raw));
	printf("nand_bytes_written              : %3d%%       sectors: %"PRIu64"\n",
			smart->nand_bytes_written.norm,
			int48_to_long(smart->nand_bytes_written.raw));
	printf("host_bytes_written              : %3d%%       sectors: %"PRIu64"\n",
			smart->host_bytes_written.norm,
			int48_to_long(smart->host_bytes_written.raw));
	printf("raid_recover_cnt                : %3d%%       %"PRIu64"\n",
			smart->raid_recover_cnt.norm,
			int48_to_long(smart->raid_recover_cnt.raw));
	printf("read_ecc_cnt                    : %3d%%       %"PRIu64"\n",
			smart->read_ecc_cnt.norm,
			int48_to_long(smart->read_ecc_cnt.raw));
	printf("prog_timeout_cnt                : %3d%%       %"PRIu64"\n",
			smart->prog_timeout_cnt.norm,
			int48_to_long(smart->prog_timeout_cnt.raw));
	printf("erase_timeout_cnt               : %3d%%       %"PRIu64"\n",
			smart->erase_timeout_cnt.norm,
			int48_to_long(smart->erase_timeout_cnt.raw));
	printf("read_timeout_cnt                : %3d%%       %"PRIu64"\n",
			smart->read_timeout_cnt.norm,
			int48_to_long(smart->read_timeout_cnt.raw));
	printf("non_media_crc_err_cnt           : %3d%%       %" PRIu64 "\n",
	       smart->non_media_crc_err_cnt.norm,
	       int48_to_long(smart->non_media_crc_err_cnt.raw));
	printf("compression_path_err_cnt        : %3d%%       %" PRIu64 "\n",
	       smart->compression_path_err_cnt.norm,
	       int48_to_long(smart->compression_path_err_cnt.raw));
	printf("out_of_space_flag               : %3d%%       %" PRIu64 "\n",
	       smart->out_of_space_flag.norm,
	       int48_to_long(smart->out_of_space_flag.raw));
	printf("phy_capacity_used_ratio         : %3d%%       %" PRIu64 "\n",
	       smart->physical_usage_ratio.norm,
	       int48_to_long(smart->physical_usage_ratio.raw));
	printf("grown_bb_count                  : %3d%%       %" PRIu64 "\n",
	       smart->grown_bb.norm, int48_to_long(smart->grown_bb.raw));


}

static int get_additional_smart_log(int argc, char **argv, struct command *cmd, struct plugin *plugin)
{
	struct nvme_additional_smart_log smart_log;
	char *desc =
	    "Get ScaleFlux vendor specific additional smart log (optionally, for the specified namespace), and show it.";
	const char *namespace = "(optional) desired namespace";
	const char *raw = "dump output in binary format";
	const char *json = "Dump output in json format";
	struct nvme_dev *dev;
	struct config {
		__u32 namespace_id;
		bool  raw_binary;
		bool  json;
	};
	int err;

	struct config cfg = {
		.namespace_id = 0xffffffff,
	};

	OPT_ARGS(opts) = {
		OPT_UINT("namespace-id", 'n', &cfg.namespace_id, namespace),
		OPT_FLAG("raw-binary",	 'b', &cfg.raw_binary,	 raw),
		OPT_FLAG("json",		 'j', &cfg.json,		 json),
		OPT_END()
	};


	err = parse_and_open(&dev, argc, argv, desc, opts);
	if (err)
		return err;

	err = nvme_get_nsid_log(dev_fd(dev), false, 0xca, cfg.namespace_id,
				sizeof(smart_log), (void *)&smart_log);
	if (!err) {
		if (cfg.json)
			show_sfx_smart_log_jsn(&smart_log, cfg.namespace_id,
					       dev->name);
		else if (!cfg.raw_binary)
			show_sfx_smart_log(&smart_log, cfg.namespace_id,
					   dev->name);
		else
			d_raw((unsigned char *)&smart_log, sizeof(smart_log));
	} else if (err > 0) {
		nvme_show_status(err);
	}
	dev_close(dev);
	return err;
}

static void show_lat_stats_vanda(struct sfx_lat_stats_vanda *stats, int write)
{
	int i;

	printf("ScaleFlux IO %s Command Latency Statistics\n", write ? "Write" : "Read");
	printf("-------------------------------------\n");
	printf("Major Revision : %u\n", stats->maj);
	printf("Minor Revision : %u\n", stats->min);

	printf("\nGroup 1: Range is 0-1ms, step is 32us\n");
	for (i = 0; i < 32; i++)
		printf("Bucket %2d: %u\n", i, stats->bucket_1[i]);

	printf("\nGroup 2: Range is 1-32ms, step is 1ms\n");
	for (i = 0; i < 31; i++)
		printf("Bucket %2d: %u\n", i, stats->bucket_2[i]);

	printf("\nGroup 3: Range is 32ms-1s, step is 32ms:\n");
	for (i = 0; i < 31; i++)
		printf("Bucket %2d: %u\n", i, stats->bucket_3[i]);

	printf("\nGroup 4: Range is 1s-2s:\n");
	printf("Bucket %2d: %u\n", 0, stats->bucket_4[0]);

	printf("\nGroup 5: Range is 2s-4s:\n");
	printf("Bucket %2d: %u\n", 0, stats->bucket_5[0]);

	printf("\nGroup 6: Range is 4s+:\n");
	printf("Bucket %2d: %u\n", 0, stats->bucket_6[0]);
}

static void show_lat_stats_myrtle(struct sfx_lat_stats_myrtle *stats, int write)
{
	int i;

	printf("ScaleFlux IO %s Command Latency Statistics\n", write ? "Write" : "Read");
	printf("-------------------------------------\n");
	printf("Major Revision : %u\n", stats->maj);
	printf("Minor Revision : %u\n", stats->min);

	printf("\nGroup 1: Range is 0us~63us, step 1us\n");
	for (i = 0; i < 64; i++)
		printf("Bucket %2d: %u\n", i, stats->bucket_1[i]);

	printf("\nGroup 2: Range is 63us~127us, step 1us\n");
	for (i = 0; i < 64; i++)
		printf("Bucket %2d: %u\n", i, stats->bucket_2[i]);

	printf("\nGroup 3: Range is 127us~255us, step 2us\n");
	for (i = 0; i < 64; i++)
		printf("Bucket %2d: %u\n", i, stats->bucket_3[i]);

	printf("\nGroup 4: Range is 255us~510us, step 4us\n");
	for (i = 0; i < 64; i++)
		printf("Bucket %2d: %u\n", i, stats->bucket_4[i]);

	printf("\nGroup 5: Range is 510us~1.02ms step\n");
	for (i = 0; i < 64; i++)
		printf("Bucket %2d: %u\n", i, stats->bucket_5[i]);

	printf("\nGroup 6: Range is 1.02ms~2.04ms step 16us\n");
	for (i = 0; i < 64; i++)
		printf("Bucket %2d: %u\n", i, stats->bucket_6[i]);

	printf("\nGroup 7: Range is 2.04ms~4.08ms step 32us\n");
	for (i = 0; i < 64; i++)
		printf("Bucket %2d: %u\n", i, stats->bucket_7[i]);

	printf("\nGroup 8: Range is 4.08ms~8.16ms step 64us\n");
	for (i = 0; i < 64; i++)
		printf("Bucket %2d: %u\n", i, stats->bucket_8[i]);

	printf("\nGroup 9: Range is 8.16ms~16.32ms step 128us\n");
	for (i = 0; i < 64; i++)
		printf("Bucket %2d: %u\n", i, stats->bucket_9[i]);

	printf("\nGroup 10: Range is 16.32ms~32.64ms step 256us\n");
	for (i = 0; i < 64; i++)
		printf("Bucket %2d: %u\n", i, stats->bucket_10[i]);

	printf("\nGroup 11: Range is 32.64ms~65.28ms step 512us\n");
	for (i = 0; i < 64; i++)
		printf("Bucket %2d: %u\n", i, stats->bucket_11[i]);

	printf("\nGroup 12: Range is 65.28ms~130.56ms step 1.024ms\n");
	for (i = 0; i < 64; i++)
		printf("Bucket %2d: %u\n", i, stats->bucket_12[i]);

	printf("\nGroup 13: Range is 130.56ms~261.12ms step 2.048ms\n");
	for (i = 0; i < 64; i++)
		printf("Bucket %2d: %u\n", i, stats->bucket_13[i]);

	printf("\nGroup 14: Range is 261.12ms~522.24ms step 4.096ms\n");
	for (i = 0; i < 64; i++)
		printf("Bucket %2d: %u\n", i, stats->bucket_14[i]);

	printf("\nGroup 15: Range is 522.24ms~1.04s step 8.192ms\n");
	for (i = 0; i < 64; i++)
		printf("Bucket %2d: %u\n", i, stats->bucket_15[i]);

	printf("\nGroup 16: Range is 1.04s~2.09s step 16.384ms\n");
	for (i = 0; i < 64; i++)
		printf("Bucket %2d: %u\n", i, stats->bucket_16[i]);

	printf("\nGroup 17: Range is 2.09s~4.18s step 32.768ms\n");
	for (i = 0; i < 64; i++)
		printf("Bucket %2d: %u\n", i, stats->bucket_17[i]);

	printf("\nGroup 18: Range is 4.18s~8.36s step 65.536ms\n");
	for (i = 0; i < 64; i++)
		printf("Bucket %2d: %u\n", i, stats->bucket_18[i]);

	printf("\nGroup 19: Range is 8.36s~ step 131.072ms\n");
	for (i = 0; i < 64; i++)
		printf("Bucket %2d: %u\n", i, stats->bucket_19[i]);

	printf("\nAverage latency statistics %" PRIu64 "\n",
	       (uint64_t)stats->average);
}


static int get_lat_stats_log(int argc, char **argv, struct command *cmd, struct plugin *plugin)
{
	struct sfx_lat_stats stats;
	char *desc = "Get ScaleFlux Latency Statistics log and show it.";
	const char *raw = "dump output in binary format";
	const char *write = "Get write statistics (read default)";
	struct nvme_dev *dev;
	struct config {
		bool raw_binary;
		bool write;
	};
	int err;

	struct config cfg = {
	};

	OPT_ARGS(opts) = {
		OPT_FLAG("write",	   'w', &cfg.write,		 write),
		OPT_FLAG("raw-binary", 'b', &cfg.raw_binary, raw),
		OPT_END()
	};

	err = parse_and_open(&dev, argc, argv, desc, opts);
	if (err)
		return err;

	err = nvme_get_log_simple(dev_fd(dev), cfg.write ? 0xc3 : 0xc1,
				  sizeof(stats), (void *)&stats);
	if (!err) {
		if ((stats.ver.maj == VANDA_MAJOR_IDX) && (stats.ver.min == VANDA_MINOR_IDX)) {
			if (!cfg.raw_binary)
				show_lat_stats_vanda(&stats.vanda, cfg.write);
			else
				d_raw((unsigned char *)&stats.vanda, sizeof(struct sfx_lat_stats_vanda));
		} else if ((stats.ver.maj == MYRTLE_MAJOR_IDX) && (stats.ver.min == MYRTLE_MINOR_IDX)) {
			if (!cfg.raw_binary)
				show_lat_stats_myrtle(&stats.myrtle, cfg.write);
			else
				d_raw((unsigned char *)&stats.myrtle, sizeof(struct sfx_lat_stats_myrtle));
		} else {
			printf("ScaleFlux IO %s Command Latency Statistics Invalid Version Maj %d Min %d\n",
				    cfg.write ? "Write" : "Read", stats.ver.maj, stats.ver.min);
		}
	} else if (err > 0) {
		nvme_show_status(err);
	}
	dev_close(dev);
	return err;
}

int sfx_nvme_get_log(int fd, __u32 nsid, __u8 log_id, __u32 data_len, void *data)
{
	struct nvme_passthru_cmd cmd = {
		.opcode		   = nvme_admin_get_log_page,
		.nsid		 = nsid,
		.addr		 = (__u64)(uintptr_t) data,
		.data_len	 = data_len,
	};
	__u32 numd = (data_len >> 2) - 1;
	__u16 numdu = numd >> 16, numdl = numd & 0xffff;

	cmd.cdw10 = log_id | (numdl << 16);
	cmd.cdw11 = numdu;

	return nvme_submit_admin_passthru(fd, &cmd, NULL);
}

/**
 * @brief	get bb table through admin_passthru
 *
 * @param fd
 * @param buf
 * @param size
 *
 * @return -1 fail ; 0 success
 */
static int get_bb_table(int fd, __u32 nsid, unsigned char *buf, __u64 size)
{
	if (fd < 0 || !buf || size != 256*4096*sizeof(unsigned char)) {
		fprintf(stderr, "Invalid Param \r\n");
		return -EINVAL;
	}

	return sfx_nvme_get_log(fd, nsid, SFX_LOG_BBT, size, (void *)buf);
}

/**
 * @brief display bb table
 *
 * @param bd_table		buffer that contain bb table dumped from driver
 * @param table_size	buffer size (BYTES), should at least has 8 bytes for mf_bb_count and grown_bb_count
 */
static void bd_table_show(unsigned char *bd_table, __u64 table_size)
{
	__u32 mf_bb_count = 0;
	__u32 grown_bb_count = 0;
	__u32 total_bb_count = 0;
	__u32 remap_mfbb_count = 0;
	__u32 remap_gbb_count = 0;
	__u64 *bb_elem;
	__u64 *elem_end = (__u64 *)(bd_table + table_size);
	__u64 i;

	/*buf should at least have 8bytes for mf_bb_count & total_bb_count*/
	if (!bd_table || table_size < sizeof(__u64))
		return;

	mf_bb_count = *((__u32 *)bd_table);
	grown_bb_count = *((__u32 *)(bd_table + sizeof(__u32)));
	total_bb_count = *((__u32 *)(bd_table + 2 * sizeof(__u32)));
	remap_mfbb_count = *((__u32 *)(bd_table + 3 * sizeof(__u32)));
	remap_gbb_count = *((__u32 *)(bd_table + 4 * sizeof(__u32)));
	bb_elem = (__u64 *)(bd_table + 5 * sizeof(__u32));

	printf("Bad Block Table\n");
	printf("MF_BB_COUNT:           %u\n", mf_bb_count);
	printf("GROWN_BB_COUNT:        %u\n", grown_bb_count);
	printf("TOTAL_BB_COUNT:        %u\n", total_bb_count);
	printf("REMAP_MFBB_COUNT:      %u\n", remap_mfbb_count);
	printf("REMAP_GBB_COUNT:       %u\n", remap_gbb_count);

	printf("REMAP_MFBB_TABLE [");
	i = 0;
	while (bb_elem < elem_end && i < remap_mfbb_count) {
		printf(" 0x%"PRIx64"", (uint64_t)*(bb_elem++));
		i++;
	}
	printf(" ]\n");

	printf("REMAP_GBB_TABLE [");
	i = 0;
	while (bb_elem < elem_end && i < remap_gbb_count) {
		printf(" 0x%"PRIx64"", (uint64_t)*(bb_elem++));
		i++;
	}
	printf(" ]\n");
}

/**
 * @brief	"hooks of sfx get-bad-block"
 *
 * @param argc
 * @param argv
 * @param cmd
 * @param plugin
 *
 * @return
 */
static int sfx_get_bad_block(int argc, char **argv, struct command *cmd, struct plugin *plugin)
{
	const __u64 buf_size = 256*4096*sizeof(unsigned char);
	unsigned char *data_buf;
	struct nvme_dev *dev;
	int err = 0;

	char *desc = "Get bad block table of sfx block device.";

	OPT_ARGS(opts) = {
		OPT_END()
	};

	err = parse_and_open(&dev, argc, argv, desc, opts);
	if (err)
		return err;

	data_buf = malloc(buf_size);
	if (!data_buf) {
		fprintf(stderr, "malloc fail, errno %d\r\n", errno);
		dev_close(dev);
		return -1;
	}

	err = get_bb_table(dev_fd(dev), 0xffffffff, data_buf, buf_size);
	if (err < 0) {
		perror("get-bad-block");
	} else if (err) {
		nvme_show_status(err);
	} else {
		bd_table_show(data_buf, buf_size);
		printf("ScaleFlux get bad block table: success\n");
	}

	free(data_buf);
	dev_close(dev);
	return 0;
}

static void show_cap_info(struct sfx_freespace_ctx *ctx)
{

	printf("logic            capacity:%5lluGB(0x%"PRIx64")\n",
			IDEMA_CAP2GB(ctx->user_space), (uint64_t)ctx->user_space);
	printf("provisioned      capacity:%5lluGB(0x%"PRIx64")\n",
			IDEMA_CAP2GB(ctx->phy_space), (uint64_t)ctx->phy_space);
	printf("free provisioned capacity:%5lluGB(0x%"PRIx64")\n",
			IDEMA_CAP2GB(ctx->free_space), (uint64_t)ctx->free_space);
	printf("used provisioned capacity:%5lluGB(0x%"PRIx64")\n",
			IDEMA_CAP2GB(ctx->phy_space) - IDEMA_CAP2GB(ctx->free_space),
			(uint64_t)(ctx->phy_space - ctx->free_space));
	printf("map_unit                 :0x%"PRIx64"K\n", (uint64_t)(ctx->map_unit * 4));
}

static int query_cap_info(int argc, char **argv, struct command *cmd, struct plugin *plugin)
{
	struct sfx_freespace_ctx ctx = { 0 };
	char *desc = "query current capacity info";
	const char *raw = "dump output in binary format";
	struct nvme_dev *dev;
	struct config {
		bool  raw_binary;
	};
	struct config cfg;
	int err = 0;

	OPT_ARGS(opts) = {
		OPT_FLAG("raw-binary", 'b', &cfg.raw_binary, raw),
		OPT_END()
	};

	err = parse_and_open(&dev, argc, argv, desc, opts);
	if (err)
		return err;

	if (nvme_query_cap(dev_fd(dev), 0xffffffff, sizeof(ctx), &ctx)) {
		perror("sfx-query-cap");
		err = -1;
	}

	if (!err) {
		if (!cfg.raw_binary)
			show_cap_info(&ctx);
		else
			d_raw((unsigned char *)&ctx, sizeof(ctx));
	}
	dev_close(dev);
	return err;
}

static int change_sanity_check(int fd, __u64 trg_in_4k, int *shrink)
{
	struct sfx_freespace_ctx freespace_ctx = { 0 };
	struct sysinfo s_info;
	__u64 mem_need = 0;
	__u64 cur_in_4k = 0;
	__u64 provisioned_cap_4k = 0;
	int extend = 0;

	if (nvme_query_cap(fd, 0xffffffff, sizeof(freespace_ctx), &freespace_ctx))
		return -1;

	/*
	 * capacity illegal check
	 */
	provisioned_cap_4k = freespace_ctx.phy_space >>
			    (SFX_PAGE_SHIFT - SECTOR_SHIFT);
	if (trg_in_4k < provisioned_cap_4k ||
	    trg_in_4k > ((__u64)provisioned_cap_4k * 4)) {
		fprintf(stderr,
			"WARNING: Only support 1.0~4.0 x provisioned capacity!\n");
		if (trg_in_4k < provisioned_cap_4k)
			fprintf(stderr,
				"WARNING: The target capacity is less than 1.0 x provisioned capacity!\n");
		else
			fprintf(stderr,
				"WARNING: The target capacity is larger than 4.0 x provisioned capacity!\n");
		return -1;
	}
	if (trg_in_4k > ((__u64)provisioned_cap_4k*4)) {
		fprintf(stderr, "WARNING: the target capacity is too large\n");
		return -1;
	}

	/*
	 * check whether mem enough if extend
	 */
	cur_in_4k = freespace_ctx.user_space >> (SFX_PAGE_SHIFT - SECTOR_SHIFT);
	extend = (cur_in_4k <= trg_in_4k);
	if (extend) {
		if (sysinfo(&s_info) < 0) {
			printf("change-cap query mem info fail\n");
			return -1;
		}
		mem_need = (trg_in_4k - cur_in_4k) * 8;
		if (s_info.freeram <= 10 || mem_need > s_info.freeram) {
			fprintf(stderr,
			    "WARNING: Free memory is not enough! Please drop cache or extend more memory and retry\n"
			    "WARNING: Memory needed is %"PRIu64", free memory is %"PRIu64"\n",
			    (uint64_t)mem_need, (uint64_t)s_info.freeram);
			return -1;
		}
	}
	*shrink = !extend;

	return 0;
}

/**
 * @brief prompt and get user confirm input
 *
 * @param str, prompt string
 *
 * @return 0, canceled; 1 confirmed
 */
static int sfx_confirm_change(const char *str)
{
	unsigned char confirm;

	fprintf(stderr, "WARNING: %s.\n"
			"Use the force [--force] option to suppress this warning.\n", str);

	fprintf(stderr, "Confirm Y/y, Others cancel:\n");
	confirm = (unsigned char)fgetc(stdin);
	if (confirm != 'y' && confirm != 'Y') {
		fprintf(stderr, "Canceled.\n");
		return 0;
	}
	fprintf(stderr, "Sending operation ...\n");
	return 1;
}

static int change_cap(int argc, char **argv, struct command *cmd, struct plugin *plugin)
{
	char *desc = "dynamic change capacity";
	const char *cap_gb = "cap size in GB";
	const char *cap_byte = "cap size in byte";
	const char *force = "The \"I know what I'm doing\" flag, skip confirmation before sending command";
	struct nvme_dev *dev;
	__u64 cap_in_4k = 0;
	__u64 cap_in_sec = 0;
	int shrink = 0;
	int err = -1;

	struct config {
		__u64 cap_in_byte;
		__u32 capacity_in_gb;
		bool  force;
	};

	struct config cfg = {
	.cap_in_byte = 0,
	.capacity_in_gb = 0,
	.force = 0,
	};

	OPT_ARGS(opts) = {
		OPT_UINT("cap",			'c',	&cfg.capacity_in_gb,	cap_gb),
		OPT_SUFFIX("cap-byte",	'z',	&cfg.cap_in_byte,		cap_byte),
		OPT_FLAG("force",		'f',	&cfg.force,				force),
		OPT_END()
	};

	err = parse_and_open(&dev, argc, argv, desc, opts);
	if (err)
		return err;

	cap_in_sec = IDEMA_CAP(cfg.capacity_in_gb);
	cap_in_4k = cap_in_sec >> 3;
	if (cfg.cap_in_byte)
		cap_in_4k = cfg.cap_in_byte >> 12;
	printf("%dG %"PRIu64"B %"PRIu64" 4K\n",
		cfg.capacity_in_gb, (uint64_t)cfg.cap_in_byte, (uint64_t)cap_in_4k);

	if (change_sanity_check(dev_fd(dev), cap_in_4k, &shrink)) {
		printf("ScaleFlux change-capacity: fail\n");
		dev_close(dev);
		return err;
	}

	if (!cfg.force && shrink && !sfx_confirm_change("Changing Cap may irrevocably delete this device's data")) {
		dev_close(dev);
		return 0;
	}

	err = nvme_change_cap(dev_fd(dev), 0xffffffff, cap_in_4k);
	if (err < 0) {
		perror("sfx-change-cap");
	} else if (err) {
		nvme_show_status(err);
	} else {
		printf("ScaleFlux change-capacity: success\n");
		ioctl(dev_fd(dev), BLKRRPART);
	}
	dev_close(dev);
	return err;
}

static int sfx_verify_chr(int fd)
{
	static struct stat nvme_stat;
	int err = fstat(fd, &nvme_stat);

	if (err < 0) {
		perror("fstat");
		return errno;
	}
	if (!S_ISCHR(nvme_stat.st_mode)) {
		fprintf(stderr,
			"Error: requesting clean card on non-controller handle\n");
		return -ENOTBLK;
	}
	return 0;
}

static int sfx_clean_card(int fd)
{
	int ret;

	ret = sfx_verify_chr(fd);
	if (ret)
		return ret;
	ret = ioctl(fd, NVME_IOCTL_CLR_CARD);
	if (ret)
		perror("Ioctl Fail.");
	else
		printf("ScaleFlux clean card success\n");

	return ret;
}

char *sfx_feature_to_string(int feature)
{
	switch (feature) {
	case SFX_FEAT_ATOMIC:
		return "ATOMIC";
	case SFX_FEAT_UP_P_CAP:
		return "UPDATE_PROVISION_CAPACITY";
	default:
		return "Unknown";
	}
}

static int sfx_set_feature(int argc, char **argv, struct command *cmd, struct plugin *plugin)
{
	char *desc = "ScaleFlux internal set features\n"
				 "feature id 1: ATOMIC\n"
				 "value 0: Disable atomic write\n"
				 "	1: Enable atomic write";
	const char *value = "new value of feature (required)";
	const char *feature_id = "hex feature name (required)";
	const char *namespace_id = "desired namespace";
	const char *force = "The \"I know what I'm doing\" flag, skip confirmation before sending command";
	struct nvme_dev *dev;
	struct nvme_id_ns ns;
	int err = 0;

	struct config {
		__u32 namespace_id;
		__u32 feature_id;
		__u32 value;
		bool  force;
	};
	struct config cfg = {
		.namespace_id = 1,
		.feature_id = 0,
		.value = 0,
		.force = 0,
	};

	OPT_ARGS(opts) = {
		OPT_UINT("namespace-id",		'n',	&cfg.namespace_id,		namespace_id),
		OPT_UINT("feature-id",			'f',	&cfg.feature_id,		feature_id),
		OPT_UINT("value",			'v',	&cfg.value,			value),
		OPT_FLAG("force",			's',	&cfg.force,			force),
		OPT_END()
	};

	err = parse_and_open(&dev, argc, argv, desc, opts);
	if (err)
		return err;

	if (!cfg.feature_id) {
		fprintf(stderr, "feature-id required param\n");
		dev_close(dev);
		return -EINVAL;
	}

	if (cfg.feature_id == SFX_FEAT_CLR_CARD) {
		/*Warning for clean card*/
		if (!cfg.force && !sfx_confirm_change("Going to clean device's data, confirm umount fs and try again")) {
			dev_close(dev);
			return 0;
		} else {
			return sfx_clean_card(dev_fd(dev));
		}

	}

	if (cfg.feature_id == SFX_FEAT_ATOMIC && cfg.value) {
		if (cfg.namespace_id != 0xffffffff) {
			err = nvme_identify_ns(dev_fd(dev), cfg.namespace_id,
					       &ns);
			if (err) {
				if (err < 0)
					perror("identify-namespace");
				else
					nvme_show_status(err);
				dev_close(dev);
				return err;
			}
			/*
			 * atomic only support with sector-size = 4k now
			 */
			if ((ns.flbas & 0xf) != 1) {
				printf("Please change-sector size to 4K, then retry\n");
				dev_close(dev);
				return -EFAULT;
			}
		}
	} else if (cfg.feature_id == SFX_FEAT_UP_P_CAP) {
		if (cfg.value <= 0) {
			fprintf(stderr, "Invalid Param\n");
			dev_close(dev);
			return -EINVAL;
		}

		/*Warning for change pacp by GB*/
		if (!cfg.force && !sfx_confirm_change("Changing physical capacity may irrevocably delete this device's data")) {
			dev_close(dev);
			return 0;
		}
	}

	err = nvme_sfx_set_features(dev_fd(dev), cfg.namespace_id,
				    cfg.feature_id,
				    cfg.value);

	if (err < 0) {
		perror("ScaleFlux-set-feature");
		dev_close(dev);
		return errno;
	} else if (!err) {
		printf("ScaleFlux set-feature:%#02x (%s), value:%d\n", cfg.feature_id,
			sfx_feature_to_string(cfg.feature_id), cfg.value);
	} else if (err > 0) {
		nvme_show_status(err);
	}

	dev_close(dev);
	return err;
}

static int sfx_get_feature(int argc, char **argv, struct command *cmd, struct plugin *plugin)
{
	char *desc = "ScaleFlux internal set features\n"
				 "feature id 1: ATOMIC";
	const char *feature_id = "hex feature name (required)";
	const char *namespace_id = "desired namespace";
	struct nvme_dev *dev;
	__u32 result = 0;
	int err = 0;

	struct config {
		__u32 namespace_id;
		__u32 feature_id;
	};
	struct config cfg = {
		.namespace_id = 0,
		.feature_id = 0,
	};

	OPT_ARGS(opts) = {
		OPT_UINT("namespace-id",		'n',	&cfg.namespace_id,		namespace_id),
		OPT_UINT("feature-id",			'f',	&cfg.feature_id,		feature_id),
		OPT_END()
	};

	err = parse_and_open(&dev, argc, argv, desc, opts);
	if (err)
		return err;

	if (!cfg.feature_id) {
		fprintf(stderr, "feature-id required param\n");
		dev_close(dev);
		return -EINVAL;
	}

	err = nvme_sfx_get_features(dev_fd(dev), cfg.namespace_id,
				    cfg.feature_id, &result);
	if (err < 0) {
		perror("ScaleFlux-get-feature");
		dev_close(dev);
		return errno;
	} else if (!err) {
		printf("ScaleFlux get-feature:%02x (%s), value:%d\n", cfg.feature_id,
			sfx_feature_to_string(cfg.feature_id), result);
	} else if (err > 0) {
		nvme_show_status(err);
	}

	dev_close(dev);
	return err;

}

static int nvme_parse_evtlog(void *pevent_log_info, __u32 log_len, char *output)
{
	__u32 offset = 0;
	__u32 length = log_len;
	__u16 fw_core;
	__u64 fw_time;
	__u8  code_level;
	__u8  code_type;
	char  str_buffer[512];
	__u32 str_pos;
	FILE *fd;
	int   err = 0;

	enum sfx_evtlog_level {
		sfx_evtlog_level_warning,
		sfx_evtlog_level_error,
	};

	const char *sfx_evtlog_warning[4] = {
		"RESERVED",
		"TOO_MANY_BB",
		"LOW_SPACE",
		"HIGH_TEMPERATURE"
	};

	const char *sfx_evtlog_error[14] = {
		"RESERVED",
		"HAS_ASSERT",
		"HAS_PANIC_DUMP",
		"INVALID_FORMAT_CAPACITY",
		"MAT_FAILED",
		"FREEZE_DUE_TO_RECOVERY_FAILED",
		"RFS_BROKEN",
		"MEDIA_ERR_ON_PAGE_IN",
		"MEDIA_ERR_ON_MPAGE_HEADER",
		"CAPACITOR_BROKEN",
		"READONLY_DUE_TO_RECOVERY_FAILED",
		"RD_ERR_IN_GSD_RECOVERY",
		"RD_ERR_ON_PF_RECOVERY",
		"MEDIA_ERR_ON_FULL_RECOVERY"
	};

	struct sfx_nvme_evtlog_info {
		__u16     time_stamp[4];
		__u64     magic1;
		__u8      reverse[10];
		char      evt_name[32];
		__u64     magic2;
		char      fw_ver[24];
		char      bl2_ver[32];
		__u16     code;
		__u16     assert_id;
	} __packed;

	struct sfx_nvme_evtlog_info *info = NULL;

	fd = fopen(output, "w+");
	if (!fd) {
		fprintf(stderr, "Failed to open %s file to write\n", output);
		err = ENOENT;
		goto ret;
	}

	while (length > 0) {
		info = (struct sfx_nvme_evtlog_info *)(pevent_log_info + offset);

		if ((info->magic1 == 0x474F4C545645) &&
		    (info->magic2 == 0x38B0B3ABA9BA)) {

			memset(str_buffer, 0, 512);
			str_pos = 0;

			fw_core = info->time_stamp[3];
			snprintf(str_buffer + str_pos, 16, "[%d-", fw_core);
			str_pos = strlen(str_buffer);

			fw_time = ((__u64)info->time_stamp[2] << 32) + ((__u64)info->time_stamp[1] << 16) + (__u64)info->time_stamp[0];
			convert_ts(fw_time, str_buffer + str_pos);
			str_pos = strlen(str_buffer);

			strcpy(str_buffer + str_pos, "]    event-log:\n");
			str_pos = strlen(str_buffer);

			snprintf(str_buffer + str_pos, 128,
				 "  > fw_version:         %s\n  > bl2_version:        %s\n",
				 info->fw_ver, info->bl2_ver);
			str_pos = strlen(str_buffer);

			code_level = (info->code & 0x100) >> 8;
			code_type  = (info->code % 0x100);
			if (code_level == sfx_evtlog_level_warning) {
				snprintf(str_buffer + str_pos, 128,
					 "  > error_str:          [WARNING][%s]\n\n",
					 sfx_evtlog_warning[code_type]);
			} else {
				if (info->assert_id)
					snprintf(str_buffer + str_pos, 128,
						 "  > error_str:          [ERROR][%s]\n  > assert_id:          %d\n\n",
						 sfx_evtlog_error[code_type], info->assert_id);
				else
					snprintf(str_buffer + str_pos, 128,
						 "  > error_str:          [ERROR][%s]\n\n",
						 sfx_evtlog_error[code_type]);
			}
			str_pos = strlen(str_buffer);

			if (fwrite(str_buffer, 1, str_pos, fd) != str_pos) {
				fprintf(stderr, "Failed to write parse result to output file\n");
				goto close_fd;
			}
		}

		offset++;
		length--;

		if (!(offset % (log_len / 100)) || (offset == log_len))
			util_spinner("Parse", (float) (offset) / (float) (log_len));
	}

	printf("\nParse-evtlog: Success\n");

close_fd:
	fclose(fd);
ret:
	return err;
}

static int nvme_dump_evtlog(struct nvme_dev *dev, __u32 namespace_id, __u32 storage_medium,
			    char *file, bool parse, char *output)
{
	struct nvme_persistent_event_log *pevent;
	void *pevent_log_info;
	_cleanup_huge_ struct nvme_mem_huge mh = { 0, };
	__u8  lsp_base;
	__u32 offset = 0;
	__u32 length = 0;
	__u32 log_len;
	__u32 single_len;
	int  err = 0;
	FILE *fd = NULL;
	struct nvme_get_log_args args = {
		.args_size	= sizeof(args),
		.fd		= dev_fd(dev),
		.lid		= NVME_LOG_LID_PERSISTENT_EVENT,
		.nsid		= namespace_id,
		.lpo		= NVME_LOG_LPO_NONE,
		.lsp		= NVME_LOG_LSP_NONE,
		.lsi		= NVME_LOG_LSI_NONE,
		.rae		= false,
		.uuidx		= NVME_UUID_NONE,
		.csi		= NVME_CSI_NVM,
		.ot		= false,
		.len		= 0,
		.log		= NULL,
		.timeout	= NVME_DEFAULT_IOCTL_TIMEOUT,
		.result		= NULL,
	};

	if (!storage_medium) {
		lsp_base = 0;
		single_len = 64 * 1024 - 4;
	} else {
		lsp_base = 4;
		single_len = 32 * 1024;
	}

	pevent = calloc(sizeof(*pevent), sizeof(__u8));
	if (!pevent) {
		err = -ENOMEM;
		goto ret;
	}

	args.lsp = lsp_base + NVME_PEVENT_LOG_RELEASE_CTX;
	args.log = pevent;
	args.len = sizeof(*pevent);

	err = nvme_get_log(&args);
	if (err) {
		fprintf(stderr, "Unable to get evtlog lsp=0x%x, ret = 0x%x\n", args.lsp, err);
		goto free_pevent;
	}

	args.lsp = lsp_base + NVME_PEVENT_LOG_EST_CTX_AND_READ;
	err = nvme_get_log(&args);
	if (err) {
		fprintf(stderr, "Unable to get evtlog lsp=0x%x, ret = 0x%x\n", args.lsp, err);
		goto free_pevent;
	}

	log_len = le64_to_cpu(pevent->tll);
	if (log_len % 4)
		log_len = (log_len / 4 + 1) * 4;

	pevent_log_info = nvme_alloc_huge(single_len, &mh);
	if (!pevent_log_info) {
		err = -ENOMEM;
		goto free_pevent;
	}

	fd = fopen(file, "wb+");
	if (!fd) {
		fprintf(stderr, "Failed to open %s file to write\n", file);
		err = ENOENT;
		goto free_pevent;
	}

	args.lsp = lsp_base + NVME_PEVENT_LOG_READ;
	args.log = pevent_log_info;
	length = log_len;
	while (length > 0) {
		args.lpo = offset;
		if (length > single_len) {
			args.len = single_len;
		} else {
			memset(args.log, 0, args.len);
			args.len = length;
		}
		err = nvme_get_log(&args);
		if (err) {
			fprintf(stderr, "Unable to get evtlog offset=0x%x len 0x%x ret = 0x%x\n", offset, args.len, err);
			goto close_fd;
		}

		if (fwrite(args.log, 1, args.len, fd) != args.len) {
			fprintf(stderr, "Failed to write evtlog to file\n");
			goto close_fd;
		}

		offset  += args.len;
		length  -= args.len;
		util_spinner("Parse", (float) (offset) / (float) (log_len));
	}

	printf("\nDump-evtlog: Success\n");

	if (parse) {
		nvme_free_huge(&mh);
		pevent_log_info = nvme_alloc_huge(log_len, &mh);
		if (!pevent_log_info) {
			fprintf(stderr, "Failed to alloc enough memory 0x%x to parse evtlog\n", log_len);
			err = -ENOMEM;
			goto close_fd;
		}

		fclose(fd);
		fd = fopen(file, "rb");
		if (!fd) {
			fprintf(stderr, "Failed to open %s file to read\n", file);
			err = ENOENT;
			goto free_pevent;
		}
		if (fread(pevent_log_info, 1, log_len, fd) != log_len) {
			fprintf(stderr, "Failed to read evtlog to buffer\n");
			goto close_fd;
		}

		err = nvme_parse_evtlog(pevent_log_info, log_len, output);
	}

close_fd:
	fclose(fd);
free_pevent:
	free(pevent);
ret:
	return err;
}

static int sfx_dump_evtlog(int argc, char **argv, struct command *cmd, struct plugin *plugin)
{
	char *desc = "dump evtlog into file and parse";
	const char *file = "evtlog file(required)";
	const char *namespace_id = "desired namespace";
	const char *storage_medium = "evtlog storage medium\n"
				     "0: nand(default) 1: nor";
	const char *parse = "parse error & warning evtlog from evtlog file";
	const char *output = "parse result output file";
	struct nvme_dev *dev;
	int err = 0;

	struct config {
		char *file;
		__u32 namespace_id;
		__u32 storage_medium;
		bool  parse;
		char *output;
	};
	struct config cfg = {
		.file = NULL,
		.namespace_id = 0xffffffff,
		.storage_medium = 0,
		.parse = false,
		.output = NULL,
	};

	OPT_ARGS(opts) = {
		OPT_FILE("file",		    'f',	&cfg.file,		file),
		OPT_UINT("namespace_id",	    'n',	&cfg.namespace_id,	namespace_id),
		OPT_UINT("storage_medium",	    's',	&cfg.storage_medium,    storage_medium),
		OPT_FLAG("parse",	            'p',	&cfg.parse,             parse),
		OPT_FILE("output",                  'o',        &cfg.output,            output),
		OPT_END()
	};

	err = parse_and_open(&dev, argc, argv, desc, opts);
	if (err)
		goto ret;

	if (!cfg.file) {
		fprintf(stderr, "file required param\n");
		err = EINVAL;
		goto close_dev;
	}

	if (cfg.parse && !cfg.output) {
		fprintf(stderr, "output file required if evtlog need be parsed\n");
		err = EINVAL;
		goto close_dev;
	}

	err = nvme_dump_evtlog(dev, cfg.namespace_id, cfg.storage_medium, cfg.file, cfg.parse, cfg.output);

close_dev:
	dev_close(dev);
ret:
	return err;
}

static int nvme_expand_cap(struct nvme_dev *dev, __u32 namespace_id, __u64 namespace_size,
			   __u64 namespace_cap, __u32 lbaf, __u32 units)
{
	struct dirent **devices;
	char dev_name[32] = "";
	int i   = 0;
	int num = 0;
	int err = 0;

	struct sfx_expand_cap_info {
		__u64 namespace_size;
		__u64 namespace_cap;
		__u8  reserve[10];
		__u8  lbaf;
		__u8  reserve1[5];
	} __packed;

	if (S_ISCHR(dev->direct.stat.st_mode))
		snprintf(dev_name, 32, "%sn%u", dev->name, namespace_id);
	else
		strcpy(dev_name, dev->name);

	num = scandir("/dev", &devices, nvme_namespace_filter, alphasort);
	if (num <= 0) {
		err = num;
		goto ret;
	}

	if (strcmp(dev_name, devices[num-1]->d_name)) {
		fprintf(stderr, "Expand namespace not the last one\n");
		err = EINVAL;
		goto free_devices;
	}

	if (!units) {
		namespace_size = IDEMA_CAP(namespace_size) / (1 << (lbaf * 3));
		namespace_cap  = IDEMA_CAP(namespace_cap) / (1 << (lbaf * 3));
	}

	struct sfx_expand_cap_info info = {
		.namespace_size = namespace_size,
		.namespace_cap  = namespace_cap,
		.lbaf = lbaf,
	};

	struct nvme_passthru_cmd cmd = {
		.opcode		 = nvme_admin_ns_mgmt,
		.nsid		 = namespace_id,
		.addr		 = (__u64)(uintptr_t)&info,
		.data_len	 = sizeof(info),
		.cdw10       = 0x0e,
	};

	err = nvme_submit_admin_passthru(dev_fd(dev), &cmd, NULL);
	if (err) {
		fprintf(stderr, "Create ns failed\n");
		nvme_show_status(err);
		goto free_devices;
	}

free_devices:
	for (i = 0; i < num; i++)
		free(devices[i]);
	free(devices);
ret:
	return err;
}

static int sfx_expand_cap(int argc, char **argv, struct command *cmd, struct plugin *plugin)
{
	char *desc = "expand capacity";
	const char *namespace_id = "desired namespace";
	const char *namespace_size = "namespace size(required)";
	const char *namespace_cap = "namespace capacity(required)";
	const char *lbaf = "LBA format to apply\n"
			   "0: 512(default) 1: 4096";
	const char *units = "namespace size/capacity units\n"
			    "0: GB(default) 1: LBA";
	struct nvme_dev *dev;
	int err = 0;

	struct config {
		__u32 namespace_id;
		__u64 namespace_size;
		__u64 namespace_cap;
		__u32 lbaf;
		__u32 units;
	};
	struct config cfg = {
		.namespace_id = 0xffffffff,
		.lbaf = 0,
		.units = 0,
	};

	OPT_ARGS(opts) = {
		OPT_UINT("namespace_id",	    'n',	&cfg.namespace_id,	namespace_id),
		OPT_LONG("namespace_size",	    's',	&cfg.namespace_size,    namespace_size),
		OPT_LONG("namespace_cap",	    'c',	&cfg.namespace_cap,     namespace_cap),
		OPT_UINT("lbaf",	            'l',	&cfg.lbaf,              lbaf),
		OPT_UINT("units",	            'u',	&cfg.units,             units),
		OPT_END()
	};

	err = parse_and_open(&dev, argc, argv, desc, opts);
	if (err)
		goto ret;

	if (cfg.namespace_id == 0xffffffff) {
		if (S_ISCHR(dev->direct.stat.st_mode)) {
			fprintf(stderr, "namespace_id or blk device required\n");
			err = EINVAL;
			goto ret;
		} else {
			cfg.namespace_id = atoi(&dev->name[strlen(dev->name) - 1]);
		}
	}

	if (!cfg.namespace_size) {
		fprintf(stderr, "namespace_size required param\n");
		err = EINVAL;
		goto close_dev;
	}

	if (!cfg.namespace_cap) {
		fprintf(stderr, "namespace_cap required param\n");
		err = EINVAL;
		goto close_dev;
	}

	err = nvme_expand_cap(dev, cfg.namespace_id, cfg.namespace_size, cfg.namespace_cap, cfg.lbaf, cfg.units);
	if (err)
		goto close_dev;

	printf("%s: Success, create nsid:%d\n", cmd->name, cfg.namespace_id);

close_dev:
	dev_close(dev);
ret:
	return err;
}
