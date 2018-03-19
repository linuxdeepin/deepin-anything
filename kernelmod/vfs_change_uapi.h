#pragma once

#define PROCFS_NAME			"vfs_changes"

#define VC_IOCTL_MAGIC		0x81
#define VC_IOCTL_READDATA	_IOR(VC_IOCTL_MAGIC, 0, long)
#define VC_IOCTL_READSTAT	_IOR(VC_IOCTL_MAGIC, 1, long)

// on input, size means total size of data, on output, it means actual data item count
// data format: 1 byte of action, 1 byte of major, 1 byte of minor, then src, then dst (if applicable)
typedef struct __vc_ioctl_readdata_args__ {
	int size;
	char* data;
} ioctl_rd_args;

typedef struct __vc_ioctl_readstat_args__ {
	int total_changes;
	int cur_changes;
	int discarded;
	int cur_memory;
} ioctl_rs_args;
