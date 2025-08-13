ifeq (${BUILD_WITH_X264},ON)

$(call decl_mod,X264)

X264_F+=\
-DBUILD_WITH_X264=1

X264_L_A+=\
-lx264

endif