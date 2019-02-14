# Makefile for benchmarks

CXXFLAGS  = -g -Wall -std=gnu++11 -D_GNU_SOURCE $(INC) -mssse3
LDFLAGS = -no-pie
ARACHNE ?= ../../arachne-all/Arachne

LD	= g++
CC	= g++

ifneq ($(DEBUG),)
CXXFLAGS += -DDEBUG -DCCAN_LIST_DEBUG -rdynamic -O0 -ggdb
LDFLAGS += -rdynamic
else
CXXFLAGS += -DNDEBUG -O3
endif

# handy for debugging
print-%  : ; @echo $* = $($*)

tbench_src = tbench.cc
tbench_obj = $(tbench_src:.cc=.o)

tbench_linux_src = tbench_linux.cc
tbench_linux_obj = $(tbench_linux_src:.cc=.o)

LIBS_ARACHNE=-I$(ARACHNE)/include -I$(ARACHNE)/../CoreArbiter/include  -I$(ARACHNE)/../PerfUtils/include \
	-L$(ARACHNE)/lib -lArachne -L$(ARACHNE)/../CoreArbiter/lib -lCoreArbiter \
	$(ARACHNE)/../PerfUtils/lib/libPerfUtils.a -lpcrecpp -pthread

# must be first
all: tbench_linux tbench_arachne

tbench_linux: $(tbench_linux_obj)
	$(LD) -o $@ $(LDFLAGS) $(tbench_linux_obj) -lpthread

tbench_arachne: tbench_arachne.cc
	$(LD) -o $@ $(LDFLAGS) tbench_arachne.cc $(LIBS_ARACHNE)

# general build rules for all targets
src = $(tbench_linux_src)
obj = $(src:.cc=.o)
dep = $(obj:.o=.d)

ifneq ($(MAKECMDGOALS),clean)
-include $(dep)   # include all dep files in the makefile
endif

# rule to generate a dep file by using the C preprocessor
# (see man cpp for details on the -MM and -MT options)
%.d: %.cc
	@$(CC) $(CXXFLAGS) $< -MM -MT $(@:.d=.o) >$@
%.o: %.cc
	$(CC) $(CXXFLAGS) -c $< -o $@

.PHONY: clean
clean:
	rm -f $(obj) $(dep) tbench_linux tbench_arachne
