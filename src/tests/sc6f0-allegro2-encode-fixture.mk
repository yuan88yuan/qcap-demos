################## sc6f0-allegro2-encode-fixture ##################
$(call decl_mod,SC6F0_ALLEGRO2_ENCODE_FIXTURE)
$(call add_flags_mod,SC6F0_ALLEGRO2_ENCODE_FIXTURE)
$(call add_mods,SC6F0_ALLEGRO2_ENCODE_FIXTURE,ZZLAB QCAP ALLEGRO2 DRM)

SC6F0_ALLEGRO2_ENCODE_FIXTURE_E=sc6f0-allegro2-encode-fixture
TESTS+=$${SC6F0_ALLEGRO2_ENCODE_FIXTURE_e}

SC6F0_ALLEGRO2_ENCODE_FIXTURE_SRCS+=\
tests/sc6f0-allegro2-encode-fixture.cpp
