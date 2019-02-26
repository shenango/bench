BASE_DIR ?= ../..
IX_DIR ?= $(BASE_DIR)/zygos
ARACHNE_DIR ?= $(BASE_DIR)/memcached-arachne/arachne-all
SHENANGO_DIR ?= $(BASE_DIR)/shenango
INC = -I$(SHENANGO_DIR)/apps/bench -I$(ARACHNE_DIR)/Arachne/include -I$(ARACHNE_DIR)/CoreArbiter/include -I$(ARACHNE_DIR)/PerfUtils/include

CPPFLAGS = -Wall -O3 -g -MD
CXXFLAGS = -std=c++11 $(INC)
LD = $(CXX)

all: spin-ix spin-linux spin-arachne

spin-linux: spin-linux.o common-linux.o $(SHENANGO_DIR)/apps/bench/fake_worker.o
	$(CXX) -o $@ $^ -pthread -lm

spin-ix: spin-ix.o common-ix.o $(IX_DIR)/libix/libix.a $(SHENANGO_DIR)/apps/bench/fake_worker.o
	$(CXX) -o $@ $^ -pthread -lm

spin-arachne: spin-arachne.o common-arachne.o $(SHENANGO_DIR)/apps/bench/fake_worker.o
	$(LD) -o $@ $^ -pthread -lm -L$(ARACHNE_DIR)/Arachne/lib -lArachne \
	-L$(ARACHNE_DIR)/PerfUtils/lib -lPerfUtils \
	-L$(ARACHNE_DIR)/CoreArbiter/lib -lCoreArbiter -lpcrecpp

spin-ix.o: CPPFLAGS += -I$(IX_DIR)/inc -I$(IX_DIR)/libix
common-ix.o: CPPFLAGS += -I$(IX_DIR)/inc -I$(IX_DIR)/libix

clean:
	rm -f *.o *.d spin-linux spin-ix spin-arachne

-include *.d
