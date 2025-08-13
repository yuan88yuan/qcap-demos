define uppercase
$(shell echo $1 | tr a-z A-Z)
endef

define lowercase
$(shell echo $1 | tr A-Z a-z)
endef

# pathsub_o_file src,dst,file_in_src
define pathsub_o_file
$(patsubst $1/%,$2/%.o,$1/$3)
endef

# pathsub_d_file src,dst,file_in_src
define pathsub_d_file
$(patsubst $1/%,$2/%.d,$1/$3)
endef

# wildcard_folders src,folder_in_src
define wildcard_folders
$(patsubst $1/$2/%.c,$2/%.c,$(wildcard $1/$2/*.c)) \
$(patsubst $1/$2/%.cpp,$2/%.cpp,$(wildcard $1/$2/*.cpp)) \
$(patsubst $1/$2/%.cu,$2/%.cu,$(wildcard $1/$2/*.cu))
endef

# wildcard_files src,files_in_src
define wildcard_files
$(patsubst $1/%,%,$(wildcard $1/$2))
endef

# mkdir_file src
define mkdir_file
${AT} mkdir -p `dirname $1`
endef

# compile_file cc,src,dst,flags_for_cc
define compile_file
${AT} $1 $4 -MMD -MP -c $2 -o $3
endef

# nvcc_file src,dst,flags_for_nvcc
define nvcc_file
${AT} ${NVCC} $3 $(GENCODE_FLAGS) -o $2 -c $1
endef

# archive_file src,dst
define archive_file
${AT} ${AR} cr $2 $1
${AT} ${RANLIB} $2
endef

# link_file flags_for_linker,dst
define link_file
$(info Linking $2 ...)
${AT} $(CXX) -o $2 $1
endef

define _decl_mod
# module name
$1_M=$(call lowercase,$1)
# module version
$1_VERSION_MAJOR=1
$1_VERSION_MINOR=0
$1_VERSION_PATCH=0
# destination path
$1_D=$(D)/$${$1_M}.dir
# archive file
# $1_A=
# shared object file
# $1_S=
# executable file
# $1_E=
# C/CXX compiler flags (-I)
# $1_I=
# C/CXX compiler flags
# $1_F=
# CUDA compiler flags
# $1_U=
# linker flags (-Wl,-Bdynamic)
# $1_L_S=
# linker flags (-Wl,-Bstatic)
# $1_L_A=
# generic linker flags
# $1_L=
# source code (list of source files (C/CXX/CU))
# $1_SRCS=
# dependent modules (list of module names)
$1_DEPS=BASE
# params for building
# $1_INC=
# $1_CXXFLAGS=
# $1_CFLAGS=
# $1_CUFLAGS=
# $1_LDFLAGS_S=
# $1_LDFLAGS_A=
# $1_LDFLAGS=
MODS+=$1
endef

define decl_mod
$(eval $(call _decl_mod,$1))
endef

define _trace_mod
$(info -------- $1 params --------)
$(info ---- $1_M=${$1_M})
$(info ---- $1_D=${$1_D})
$(info ---- $1_A=${$1_A})
$(info ---- $1_S=${$1_S})
$(info ---- $1_E=${$1_E})
$(info ---- $1_I=${$1_I})
$(info ---- $1_L_S=${$1_L_S})
$(info ---- $1_L_A=${$1_L_A})
$(info ---- $1_F=${$1_F})
$(info ---- $1_U=${$1_U})
$(info ---- $1_L=${$1_L})
$(info ---- $1_SRCS=${$1_SRCS})
$(info ---- $1_DEPS=${$1_DEPS})
$(info ---- $1_INC=${$1_INC})
$(info ---- $1_CXXFLAGS=${$1_CXXFLAGS})
$(info ---- $1_CFLAGS=${$1_CFLAGS})
$(info ---- $1_CUFLAGS=${$1_CUFLAGS})
$(info ---- $1_LDFLAGS_S=${$1_LDFLAGS_S})
$(info ---- $1_LDFLAGS_A=${$1_LDFLAGS_A})
$(info ---- $1_LDFLAGS=${$1_LDFLAGS})
$(info #### $1_objs=${$1_objs})
$(info #### $1_d=${$1_d})
$(info #### $1_a=${$1_a})
$(info #### $1_s=${$1_s})
$(info #### $1_e=${$1_e})
$(info #### $1_deps=${$1_deps})
$(info #### $1_cxxflags=${$1_cxxflags})
$(info #### $1_cflags=${$1_cflags})
$(info #### $1_cuflags=${$1_cuflags})
$(info #### $1_ldflags=${$1_ldflags})
endef

define trace_mod
$(eval $(call _trace_mod,$1))
endef

define _gen_rules
$1_deps+=$$(foreach dep,$${$1_DEPS},$${$${dep}_a}) $$(foreach dep,$${$1_DEPS},$${$${dep}_s})
-include $${${mod}_d}

endef

define _gen_compile_rule
$${$1_D}/%.cpp.o: $${S}/%.cpp
	$$(info [$1] CXX Compiling $$@ ...)
	$$(call mkdir_file,$$@)
	$$(call compile_file,$${CXX},$$<,$$@,$${$1_cxxflags})

$${$1_D}/%.c.o: $${S}/%.c
	$$(info [$1] C Compiling $$@ ...)
	$$(call mkdir_file,$$@)
	$$(call compile_file,$${CC},$$<,$$@,$${$1_cflags})

$${$1_D}/%.cu.o: $${S}/%.cu
	$$(info [$1] CUDA Compiling $$@ ...)
	$$(call mkdir_file,$$@)
	$$(call nvcc_file,$$<,$$@,$${$1_cuflags})

endef

define _gen_archive_rule
$${$1_a}: $${$1_objs} $${$1_deps}
	$$(info [$1] Building $$@ ...)
	$$(call mkdir_file,$$@)
	$$(call archive_file,$${$1_objs},$$@)

${$1_A}: $${$1_a}

endef

define _gen_shared_rule
$${$1_s}.${$1_VERSION_MAJOR}.${$1_VERSION_MINOR}.${$1_VERSION_PATCH}: $${$1_objs} $${$1_deps}
	$$(info [$1] Building $$@ ...)
	$$(call mkdir_file,$$@)
	$$(call link_file,-shared $${$1_objs} $${$1_ldflags},$$@)
	$${AT} $${STRIP} $$@

$${$1_s}.${$1_VERSION_MAJOR}: $${$1_s}.$${$1_VERSION_MAJOR}.${$1_VERSION_MINOR}.${$1_VERSION_PATCH}
	$$(call mkdir_file,$$@)
	$${AT} ln -sfr $$< $$@

$${$1_s}: $${$1_s}.${$1_VERSION_MAJOR}
	$$(call mkdir_file,$$@)
	$${AT} ln -sfr $$< $$@

${$1_S}: $${$1_s}

endef

define _gen_executable_rule
$${$1_e}: $${$1_objs} $${$1_deps}
	$$(info [$1] Building $$@ ...)
	$$(call mkdir_file,$$@)
	$$(call link_file,$${$1_objs} $${$1_ldflags},$$@)
	$${AT} $${STRIP} $$@

${$1_E}: $${$1_e}

endef

define _ld_path
-L$1 -Wl,-rpath-link=$1
endef

define _gen_mod_flags
${$1_F} $(foreach dep,${$1_DEPS},${${dep}_F}) ${$1_I} $(foreach dep,${$1_DEPS},${${dep}_I})
endef

define _gen_mod_ldflags
${${mod}_LDFLAGS} $(foreach dep,${$1_DEPS},${${dep}_L}) \
-Wl,-Bstatic ${${mod}_LDFLAGS_A} $(foreach dep,${$1_DEPS},${${dep}_L_A}) \
-Wl,-Bdynamic ${${mod}_LDFLAGS_S} $(foreach dep,${$1_DEPS},${${dep}_L_S})
endef

define _decl_mod_rules
${mod}_objs=$(foreach src,${${mod}_SRCS},$(call pathsub_o_file,${S},${${mod}_D},${src}))
${mod}_d=$(foreach src,${${mod}_SRCS},$(call pathsub_d_file,${S},${${mod}_D},${src}))

ifneq (${${mod}_A},)
${mod}_a=${D}/lib/${${mod}_A}
endif

ifneq (${${mod}_S},)
${mod}_s=${D}/lib/${${mod}_S}
endif

ifneq (${${mod}_E},)
${mod}_e=${D}/bin/${${mod}_E}
endif

${mod}_cxxflags+=${CXXFLAGS} -fPIC -fpermissive ${${mod}_CXXFLAGS} ${${mod}_INC} $(call _gen_mod_flags,${mod})
${mod}_cflags+=${CFLAGS} -fPIC ${${mod}_CFLAGS} ${${mod}_INC} $(call _gen_mod_flags,${mod})
${mod}_cuflags+=--shared ${${mod}_CUFLAGS} $(addprefix -Xcompiler ,$(filter-out -std=c++11,$(call _gen_mod_flags,${mod})))
${mod}_ldflags+=${LDFLAGS} $(call _gen_mod_ldflags,${mod})

${mod}_rules+=$$(call _gen_rules,${mod})

ifneq ($${${mod}_objs},)
${mod}_rules+=$$(call _gen_compile_rule,${mod})
endif

ifneq ($${${mod}_A},)
${mod}_rules+=$$(call _gen_archive_rule,${mod})
endif

ifneq ($${${mod}_S},)
${mod}_rules+=$$(call _gen_shared_rule,${mod})
endif

ifneq ($${${mod}_E},)
${mod}_rules+=$$(call _gen_executable_rule,${mod})
endif

endef

define _add_mod
ifeq ($${BUILD_WITH_$2},ON)
$1_FLAGS+=${$2_M}
endif
$1_DEPS+=$2
endef

define add_mod
$(eval $(call _add_mod,$1,$2))
endef

define add_mods
$(foreach mod,$2,$(call add_mod,$1,${mod}))
endef

define _add_flag
ifeq ($${BUILD_$2},ON)
$1_FLAGS+=$$(call lowercase,$2)
$1_F+=\
-DBUILD_$2=1
endif
endef

define add_flags
$(foreach flag,$2,$(eval $(call _add_flag,$1,${flag})))
endef

define INSTALL
$(info Installing...)

endef

define PACKAGE
$(info Packaging...)

endef
