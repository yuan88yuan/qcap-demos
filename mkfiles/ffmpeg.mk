BUILD_WITH_FFMPEG=ON

$(call decl_mod,FFMPEG)

FFMPEG_F+=\
-DBUILD_WITH_FFMPEG=1

FFMPEG_L_A+=\
-lavformat \
-lavfilter \
-lavcodec \
-lavutil \
-lswresample \
-lswscale \
-lpostproc \
-lfdk-aac

ifeq (${BUILD_L4T},ON)
FFMPEG_L_S+=\
-lvdpau

endif
