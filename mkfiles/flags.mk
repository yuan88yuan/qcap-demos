define _add_flags_mod
# main branch
$$(call add_flags,$1,\
LINUX \
HISIV \
DESKTOP \
L4T \
IGX \
NOVATEK)

# x86_64
$$(call add_flags,$1,\
CENTOS76_X64 \
DEBIAN105_X64 \
UBUNTU1604_X64 \
UBUNTU1804_X64 \
UBUNTU2004_X64 \
UBUNTU2404_X64)

# arm64
$$(call add_flags,$1,\
UBUNTU1804_TX2 \
L4T3250 \
L4T3411 \
L4T3510 \
L4T3620 \
IGX_R35_4 \
IGX_R36_0_7_0 \
IGX_R36_1 \
IGX_R36_3 \
XLNK_ARM64 \
RK3566_ARM64 \
KYLIN_ARM64 \
NOVATEK_ARM64)

# hisiv
$$(call add_flags,$1,\
HI3531A \
HI3531A400 \
HI3531D \
HI3531A510 \
HI3559A \
HI3559A210 \
HI3519A)

# hardware platforms
$$(call add_flags,$1,\
SC6F0 \
SC6E0 \
SC6N0 \
SC6T0 \
SC6G0)

# dev flags
$$(call add_flags,$1,\
WITH_ZZLAB)
endef

define add_flags_mod
$(eval $(call _add_flags_mod,$1))
endef