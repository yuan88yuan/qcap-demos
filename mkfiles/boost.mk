BUILD_WITH_BOOST=ON

$(call decl_mod,BOOST)

BOOST_F+=\
-DBUILD_WITH_BOOST=1 \
-DBOOST_BIND_GLOBAL_PLACEHOLDERS

BOOST_L_A+=\
-lboost_system \
-lboost_thread \
-lboost_atomic \
-lboost_chrono \
-lboost_context \
-lboost_filesystem \
-lboost_program_options \
-lboost_coroutine

