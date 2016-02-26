# Use caps for vars which users are allowed to initialise from outside (and
# CC, which is special).

cc 		= gcc 
CC 		= $(cc) 

CFLAGS		+= -std=c99 
LDFLAGS		?= 

main		= fish-screen

# Will be looped over to build <module>_cflags, <module>_ldflags, etc.
modules 	= fishutil fishutils
submodules	= fish-lib-util

pkg_names		=

fishutil_dir		= fish-lib-util
fishutil_cflags		= $(shell PKG_CONFIG_PATH=$(fishutil_dir)/pkg-config/static pkg-config --cflags fish-util)
fishutil_ldflags	= $(shell PKG_CONFIG_PATH=$(fishutil_dir)/pkg-config/static pkg-config --static --libs fish-util)
fishutils_cflags	= $(shell PKG_CONFIG_PATH=$(fishutil_dir)/pkg-config/static pkg-config --cflags fish-utils)
fishutils_ldflags	= $(shell PKG_CONFIG_PATH=$(fishutil_dir)/pkg-config/static pkg-config --static --libs fish-utils)

CFLAGS		+= -W -Wall -Wextra -I./
CFLAGS		+= $(foreach i,$(modules),$(${i}_cflags))

LDFLAGS		+= -Wl,--export-dynamic
LDFLAGS		+= $(foreach i,$(modules),$(${i}_ldflags))

hdr		= $(main).h
src		= $(main).c
obj		= 

ifneq ($(pkg_names),)
    # Check user has correct packages installed (and found by pkg-config).
    pkgs_ok := $(shell pkg-config --print-errors --exists $(pkg_names) && echo 1)
    ifneq ($(pkgs_ok),1)
        $(error Cannot find required package(s\). Please \
        check you have the above packages installed and try again.)
    endif
endif

all: submodules $(main)

submodules: 
	for i in "$(submodules)"; do \
	    cd "$$i"; \
	    make; \
	    cd ..; \
	done;

$(main): $(src) $(hdr) $(obj)
	@#echo $(CC) $(CFLAGS) $(LDFLAGS) $(obj) -o $(main)
	$(CC) $(CFLAGS) $(LDFLAGS) $(src) $(obj) -o $(main)

# Note that everything gets rebuilt if any header changes. 
# This is too tricky to try to avoid.
$(obj): %.o: %.c $(hdr)
	@echo $(CC) -c $< -o $@
	@$(CC) $(CFLAGS) -c $< -o $@

clean: 
	rm -f *.o
	rm -f *.so
	cd $(fishutil_dir) && make clean
	rm -f $(main)

mrproper: clean 
	cd $(fishutil_dir) && make mrproper

.PHONY: all clean mrproper
