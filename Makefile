dirs := $(shell find . -maxdepth 1 -type d | sort -r)
dirs := $(filter-out . ./docs ./.git ./tools, $(dirs))
dirs := $(patsubst ./%, %, $(dirs))

all:
%: 
	@for dir in $(dirs); do\
  	    make -C $$dir $@;\
	done
