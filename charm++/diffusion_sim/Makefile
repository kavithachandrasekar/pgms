-include ../../../common.mk
OPTS	= -O3 -g -std=c++14
CHARMC	= $(CHARM_HOME)/bin/charmc $(OPTS) 

OBJS = DiffusionSim.o

all: DiffusionSim

DiffusionSim: $(OBJS)
	$(CHARMC) -language charm++ -o DiffusionSim $(OBJS)

DiffusionSim.def.h DiffusionSim.decl.h: DiffusionSim.ci.stamp

DiffusionSim.ci.stamp: DiffusionSim.ci
	$(CHARMC) $<
	touch $@

DiffusionSim.o: DiffusionSim.C DiffusionSim.decl.h DiffusionSim.def.h DiffusionSim.h ../sim_headers/common_lbsim.h DiffusionNeighbors.C
	$(CHARMC) -c DiffusionSim.C

clean:
	rm -f *.decl.h *.def.h conv-host *.o DiffusionSim DiffusionSim.prj charmrun *~ *.stamp

