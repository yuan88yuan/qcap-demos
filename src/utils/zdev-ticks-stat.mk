################## zdev-ticks-stat ##################
$(call decl_mod,ZDEV_TICKS_STAT)
$(call add_flags_mod,ZDEV_TICKS_STAT)

ZDEV_TICKS_STAT_E=zdev-ticks-stat
UTILS+=$${ZDEV_TICKS_STAT_e}

ZDEV_TICKS_STAT_SRCS+=\
utils/zdev-ticks-stat.cpp
