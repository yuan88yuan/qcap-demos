ifeq (${BUILD_WITH_X11},ON)

$(call decl_mod,X11)

X11_F+=\
-DBUILD_WITH_X11=1

X11_L_S+=\
-lX11 \
-lXv

endif