// SPDX-License-Identifier: GPL-2.0-or-later
#include <fcntl.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdbool.h>
#include <inttypes.h>
#include <linux/fs.h>
#include <sys/stat.h>

#include "common.h"
#include "nvme.h"
#include "libnvme.h"
#include "nvme-print.h"
#include "sedopal_cmd.h"
#include <linux/sed-opal.h>

#define CREATE_CMD
#include "sed.h"

OPT_ARGS(no_opts) = {
	OPT_END()
};

OPT_ARGS(init_opts) = {
	OPT_FLAG("read-only", 'r', &sedopal_lock_ro,
		 "Set locking range to read-only"),
	OPT_END()
};

OPT_ARGS(key_opts) = {
	OPT_FLAG("ask-key", 'k', &sedopal_ask_key,
			"prompt for SED authentication key"),
	OPT_END()
};

OPT_ARGS(revert_opts) = {
	OPT_FLAG("destructive", 'e', &sedopal_destructive_revert,
			"destructive revert"),
	OPT_FLAG("psid", 'p', &sedopal_psid_revert, "PSID revert"),
	OPT_END()
};

OPT_ARGS(lock_opts) = {
	OPT_FLAG("read-only", 'r', &sedopal_lock_ro,
		 "Set locking range to read-only"),
	OPT_FLAG("ask-key", 'k', &sedopal_ask_key,
			"prompt for SED authentication key"),
	OPT_END()
};

OPT_ARGS(discovery_opts) = {
	OPT_FLAG("verbose", 'v', &sedopal_discovery_verbose,
		"Print extended discovery information"),
	OPT_FLAG("udev", 'u', &sedopal_discovery_udev,
		"Print locking information in form suitable for udev rules"),
	OPT_END()
};

/*
 * Open the NVMe device specified on the command line. It must be the
 * NVMe block device (e.g. /dev/nvme0n1).
 */
static int sed_opal_open_device(nvme_root_t *r, nvme_link_t *l, int argc, char **argv,
		const char *desc, struct argconfig_commandline_options *opts)
{
	int err;

	err = parse_and_open(r, l, argc, argv, desc, opts);
	if (err)
		return err;

	if (!nvme_link_is_blkdev(*l)) {
		fprintf(stderr,
			"ERROR : The NVMe block device must be specified\n");
		err = -EINVAL;
	}

	return err;
}

static int sed_opal_discover(int argc, char **argv, struct command *cmd,
		struct plugin *plugin)
{
	const char *desc = "Query SED device and display locking features";
	_cleanup_nvme_root_ nvme_root_t r = NULL;
	_cleanup_nvme_link_ nvme_link_t l = NULL;
	int err;

	err = sed_opal_open_device(&r, &l, argc, argv, desc, discovery_opts);
	if (err)
		return err;

	err = sedopal_cmd_discover(nvme_link_get_fd(l));

	return err;
}

static int sed_opal_initialize(int argc, char **argv, struct command *cmd,
		struct plugin *plugin)
{
	const char *desc = "Initialize a SED device for locking";
	_cleanup_nvme_root_ nvme_root_t r = NULL;
	_cleanup_nvme_link_ nvme_link_t l = NULL;
	int err;

	err = sed_opal_open_device(&r, &l, argc, argv, desc, init_opts);
	if (err)
		return err;

	err = sedopal_cmd_initialize(nvme_link_get_fd(l));
	if ((err != 0) && (err != -EOPNOTSUPP))
		fprintf(stderr, "initialize: SED error -  %s\n",
				sedopal_error_to_text(err));

	return err;
}

static int sed_opal_revert(int argc, char **argv, struct command *cmd,
		struct plugin *plugin)
{
	const char *desc = "Revert a SED device from locking state";
	_cleanup_nvme_root_ nvme_root_t r = NULL;
	_cleanup_nvme_link_ nvme_link_t l = NULL;
	int err;

	err = sed_opal_open_device(&r, &l, argc, argv, desc, revert_opts);
	if (err)
		return err;

	err = sedopal_cmd_revert(nvme_link_get_fd(l));
	if ((err != 0) && (err != -EOPNOTSUPP) && (err != EPERM))
		fprintf(stderr, "revert: SED error -  %s\n",
				sedopal_error_to_text(err));

	return err;
}

static int sed_opal_lock(int argc, char **argv, struct command *cmd,
		struct plugin *plugin)
{
	const char *desc = "Lock a SED device";
	_cleanup_nvme_root_ nvme_root_t r = NULL;
	_cleanup_nvme_link_ nvme_link_t l = NULL;
	int err;

	err = sed_opal_open_device(&r, &l, argc, argv, desc, lock_opts);
	if (err)
		return err;

	err = sedopal_cmd_lock(nvme_link_get_fd(l));
	if ((err != 0) && (err != -EOPNOTSUPP))
		fprintf(stderr, "lock: SED error -  %s\n",
				sedopal_error_to_text(err));

	return err;
}

static int sed_opal_unlock(int argc, char **argv, struct command *cmd,
		struct plugin *plugin)
{
	const char *desc = "Unlock a SED device";
	_cleanup_nvme_root_ nvme_root_t r = NULL;
	_cleanup_nvme_link_ nvme_link_t l = NULL;
	int err;

	err = sed_opal_open_device(&r, &l, argc, argv, desc, lock_opts);
	if (err)
		return err;

	err = sedopal_cmd_unlock(nvme_link_get_fd(l));
	if ((err != 0) && (err != -EOPNOTSUPP))
		fprintf(stderr, "unlock: SED error -  %s\n",
				sedopal_error_to_text(err));

	return err;
}

static int sed_opal_password(int argc, char **argv, struct command *cmd,
		struct plugin *plugin)
{
	int err;
	const char *desc = "Change the locking password of a SED device";
	_cleanup_nvme_root_ nvme_root_t r = NULL;
	_cleanup_nvme_link_ nvme_link_t l = NULL;

	err = sed_opal_open_device(&r, &l, argc, argv, desc, no_opts);
	if (err)
		return err;

	err = sedopal_cmd_password(nvme_link_get_fd(l));
	if ((err != 0) && (err != EPERM))
		fprintf(stderr, "password: SED error -  %s\n",
				sedopal_error_to_text(err));

	return err;
}
