###############################################################################
#
# 
#
###############################################################################

LIBRARY := x86_64_accton_as9947_36xkb
$(LIBRARY)_SUBDIR := $(dir $(lastword $(MAKEFILE_LIST)))
include $(BUILDER)/lib.mk
