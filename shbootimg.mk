LOCAL_PATH := $(call my-dir)

# Add ramdisk dependencies to kernel
TARGET_KERNEL_BINARIES: $(PRODUCT_OUT)/utilities/flash_image $(PRODUCT_OUT)/utilities/busybox $(PRODUCT_OUT)/utilities/make_ext4fs $(PRODUCT_OUT)/utilities/erase_image

# Dummy recovery image generation
$(INSTALLED_RECOVERYIMAGE_TARGET): $(recovery_ramdisk) $(MKBOOTIMG) $(recovery_kernel)
	@echo -e ${PRT_IMG}"----- Making recovery image ------"${CL_RST}
	$(call build-recoveryimage-target, $@)
	@echo -e ${PRT_IMG}"----- Made recovery image: $@ --------"${CL_RST}
