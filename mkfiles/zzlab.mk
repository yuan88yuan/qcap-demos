BUILD_WITH_ZZLAB=ON

$(call decl_mod,ZZLAB)

ZZLAB_A=lib${ZZLAB_M}.a

ZZLAB_SRC+=\
zzlab/ZzClock.cpp \
zzlab/ZzLog.cpp \
zzlab/ZzModules.cpp \
zzlab/ZzStats.cpp \
zzlab/ZzUtils.cpp \
zzlab/ZzUtils_string.cpp \
zzlab/ZzUtils_env.cpp

ZZLAB_F+=\
-DBUILD_WITH_ZZLAB=1

ZZLAB_I+=\
-I${S}/zzlab

ZZLAB_L_A+=\
-l${ZZLAB_M}
