ifeq (${BUILD_WITH_UDEV},ON)

$(call decl_mod,UDEV)

UDEV_F+=\
-DBUILD_WITH_UDEV=1

UDEV_L_S+=\
-ludev

endif
