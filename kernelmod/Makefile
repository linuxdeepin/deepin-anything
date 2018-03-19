obj-m += vfs_monitor.o
vfs_monitor-objs := vfs_utils.o arg_extractor.o vfs_change.o vfs_kretprobes.o
ccflags-y := -std=gnu99 -Wno-declaration-after-statement -O3
cwd := $(shell pwd)

kdir := /lib/modules/$(shell uname -r)/build

all:
	make -C ${kdir} M=$(cwd) modules

clean:
	make -C ${kdir} M=$(cwd) clean

