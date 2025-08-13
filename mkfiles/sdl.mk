ifeq (${BUILD_WITH_SDL},ON)

$(call decl_mod,SDL)

SDL_F+=\
-DBUILD_WITH_SDL=1

SDL_L_A+=\
-lSDL2

endif