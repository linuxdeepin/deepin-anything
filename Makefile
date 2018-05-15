VERSION := 0.0
DEB_HOST_MULTIARCH ?= $(shell dpkg-architecture -qDEB_HOST_MULTIARCH)

all:
	sed 's|@@VERSION@@|$(VERSION)|g' debian/deepin-anything-dkms.dkms.in | tee debian/deepin-anything-dkms.dkms
	make -C library all

install:
	mkdir -p $(DESTDIR)/usr/lib/$(DEB_HOST_MULTIARCH)
	cp library/bin/release/* $(DESTDIR)/usr/lib/$(DEB_HOST_MULTIARCH)
	mkdir -p $(DESTDIR)/usr/src/deepin-anything-$(VERSION)
	cp -r kernelmod/* $(DESTDIR)/usr/src/deepin-anything-$(VERSION)
	mkdir -p $(DESTDIR)/usr/lib/modules-load.d
	echo "vfs_monitor" | tee $(DESTDIR)/usr/lib/modules-load.d/anything.conf
	mkdir -p $(DESTDIR)/usr/include/deepin-anything
	cp -r library/inc/* $(DESTDIR)/usr/include/deepin-anything
	cp -r kernelmod/vfs_change_uapi.h $(DESTDIR)/usr/include/deepin-anything
	cp -r kernelmod/vfs_change_consts.h $(DESTDIR)/usr/include/deepin-anything

clean:
	rm -f debian/deepin-anything-dkms.dkms
	make -C library clean
