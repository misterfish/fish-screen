fish_util_dir = fish-lib-util/fish-util
fish_utils_dir = fish-lib-util/fish-utils

fish_util_flags = 	$(shell cat $(fish_util_dir)/flags)
fish_util_o = 		$(shell find $(fish_util_dir)/.deps -iname '*.o' | sed 's/\.deps\///')

fish_utils_flags = 	$(shell cat $(fish_utils_dir)/flags)
fish_utils_o = 		$(shell find $(fish_utils_dir)/.deps -iname '*.o' | sed 's/\.deps\///')

WARN=-Wall
FLAGS_C = -std=c99 $(WARN) -lpcre -I$(fish_util_dir) -I$(fish_utils_dir) $(fish_util_flags) $(fish_utils_flags)

all: fish-screen

fish-screen: fish-util fish-utils main.c 
	gcc $(FLAGS_C) $(fish_util_o) $(fish_utils_o) main.c -o fish-screen

fish-util: 
	sh -c 'cd fish-lib-util/fish-util; make'

fish-utils:
	sh -c 'cd fish-lib-util/fish-utils; make'

clean: 
	rm -f *.o
	rm -f fish-screen
