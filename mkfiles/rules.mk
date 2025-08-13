# global vars
AT?=@
S?=src
D?=_objs/${PLATFORM}
CXXFLAGS?=${CXXFLAGS}
CFLAGS?=${CFLAGS}
LDFLAGS?=${LDFLAGS}
CUFLAGS?=${CUFLAGS}
GIT_COMMIT_HASH=$(shell git log -1 --format=%h)
GIT_BRANCH_NAME=$(shell git rev-parse --abbrev-ref HEAD)
DOCKER_HOME?=/docker

$(info PLATFORM=${PLATFORM})
$(info GIT_COMMIT_HASH=${GIT_COMMIT_HASH})
$(info GIT_BRANCH_NAME=${GIT_BRANCH_NAME})
$(info DOCKER_HOME=${DOCKER_HOME})

# modules
include mkfiles/flags.mk
include mkfiles/mods.mk

# utils
UTILS=utils=
include ${S}/utils/utils.mk

# tests
TESTS=tests=
include ${S}/tests/tests.mk

# post rules
ifeq (${VERBOSE},ON)
$(info ---------------MODS-------------------)
$(info $(foreach mod,${MODS},${_decl_mod_rules}))
$(info --------------------------------------)
endif
$(eval $(foreach mod,${MODS},${_decl_mod_rules}))

ifeq (${VERBOSE},ON)
$(info ---------------UTILS------------------)
$(info ${UTILS})
$(info --------------------------------------)
endif
$(eval ${UTILS})

ifeq (${VERBOSE},ON)
$(info ---------------TESTS------------------)
$(info ${TESTS})
$(info --------------------------------------)
endif
$(eval ${TESTS})

INSTALL+=$(foreach install,${MODS},${${install}_INSTALL})
PACKAGE+=$(foreach package,${MODS},${${package}_PACKAGE})

.PHONY: package
package: install
	$(PACKAGE)

.PHONY: install
install: all
	$(INSTALL)

.PHONY: all
all: tests utils
	$(info Targets $^, all done.)

.PHONY: clean
clean:
	$(info Cleaning ${D} ...)
	${AT} rm ${D} -rf

.PHONY: clean-lib
clean-lib:
	$(info Cleaning ${D}/lib ...)
	${AT} rm ${D}/lib -rf

.PHONY: clean-bin
clean-bin:
	$(info Cleaning ${D}/bin ...)
	${AT} rm ${D}/bin -rf

.PHONY: utils
utils: ${utils}
	$(info Utilities $^, all done.)

.PHONY: tests
tests: ${tests}
	$(info Tests $^, all done.)

.PHONY: backup-3rdparty
backup-3rdparty:
	$(info Backup 3rdparty libs... ${PLATFORM})
	${AT} mkdir ${DOCKER_HOME}/qcap-3rdparty/${PLATFORM} -p
	${AT} cp -rpf ${SDKTARGETSYSROOT}/usr/local/qcap/* ${DOCKER_HOME}/qcap-3rdparty/${PLATFORM}/

ifeq (${VERBOSE},ON)
$(info ----------------MODS------------------)
$(info $(foreach mod,${MODS},${${mod}_rules}))
$(info --------------------------------------)
endif
$(eval $(foreach mod,${MODS},${${mod}_rules}))
