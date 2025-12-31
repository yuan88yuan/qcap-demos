ifeq (${BUILD_WITH_ALLEGRO2},ON)

$(call decl_mod,ALLEGRO2)

ALLEGRO2_F+=\
-DBUILD_WITH_ALLEGRO2=1

ALLEGRO2_I+=\
-I${DOCKER_HOME}/qcap-3rdparty/${PLATFORM}/include/allegro \
-I${DOCKER_HOME}/qcap-3rdparty/${PLATFORM}/include/allegro-omx-il

ALLEGRO2_L_S+=\
-lallegro_decode \
-lallegro_encode

endif