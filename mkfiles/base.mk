BUILD_WITH_BASE=ON

$(call decl_mod,BASE)

BASE_F+=\
-O3 \
-DBUILD_SYS=2 \
-DGIT_COMMIT_HASH='"${GIT_COMMIT_HASH}"' \
-DGIT_BRANCH_NAME='"${GIT_BRANCH_NAME}"' \
-DOS_LINUX

BASE_I+=\
-I${S}/../include \
-I${QCAP_3RDPARTY}/include

BASE_L+=$(call _ld_path,${D}/lib)
BASE_L+=$(call _ld_path,${QCAP_3RDPARTY}/lib)

BASE_L+=\
-pthread

BASE_L_S+=\
-lrt \
-ldl
