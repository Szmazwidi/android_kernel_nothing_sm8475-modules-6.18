# SPDX-License-Identifier: GPL-2.0-only

# Include if present
ifneq ($(wildcard config/qcomdisp.mk),)
  include config/qcomdisp.mk
endif

M := $(shell pwd)
DISPLAY_ROOT=$(M)
KBUILD_OPTIONS := DISPLAY_ROOT=$(DISPLAY_ROOT) CONFIG_DRM_MSM=$(CONFIG_DRM_MSM)

obj-m += msm/

all:
	$(MAKE) -C $(KERNEL_SRC) M=$(M) modules $(KBUILD_OPTIONS)

modules_install:
	$(MAKE) INSTALL_MOD_STRIP=1 -C $(KERNEL_SRC) M=$(M) modules_install

%:
	$(MAKE) -C $(KERNEL_SRC) M=$(M) $@ $(KBUILD_OPTIONS)

clean:
	rm -f *.o *.ko *.mod.c *.mod.o *~ .*.cmd Module.symvers
	rm -rf .tmp_versions
