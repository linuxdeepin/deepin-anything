obj-m += vfs_monitor.o
vfs_monitor-objs := arg_extractor.o event_merge.o event.o module.o vfs_kretprobes.o vfs_fsnotify.o vfs_genl.o
ccflags-y := -std=gnu99 -Wno-declaration-after-statement -O3
cwd := $(shell pwd)

kdir := /lib/modules/$(shell uname -r)/build

all:
	make -C ${kdir} M=$(cwd) modules

clean:
	make -C ${kdir} M=$(cwd) clean

