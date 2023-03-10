# SPDX-License-Identifier: (GPL-2.0+ OR MIT)

MODULE_NAME = amlogic_cve

obj-m += $(MODULE_NAME).o

$(MODULE_NAME)-y = src/cve.o

M_PATH := $(shell dirname $(lastword $(MAKEFILE_LIST)))

LOCAL_INCLUDES := -I$(M_PATH)/inc

ifeq ($(PLATFORM_VERSION),)
    ccflags-y += -DCONFIG_T7
else
    ccflags-y += -DCONFIG_$(PLATFORM_VERSION)
endif

KBUILD_CFLAGS_MODULE += $(GKI_EXT_MODULE_PREDEFINE)

ccflags-y += $(LOCAL_INCLUDES)

EXTRA_CFLAGS += $(LOCAL_INCLUDES)

all:
	@$(MAKE) -C $(KERNEL_SRC) M=$(M) modules

modules_install:
	@$(MAKE) M=$(M) -C $(KERNEL_SRC) modules_install
	mkdir -p ${OUT_DIR}/../vendor_lib
	cp $(OUT_DIR)/$(M)/*.ko ${OUT_DIR}/../vendor_lib/modules/

clean:
	@$(MAKE) -C $(KERNEL_SRC) M=$(M) clean
