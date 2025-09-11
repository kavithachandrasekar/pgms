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

DiffusionSim.o: DiffusionSim.C DiffusionSim.decl.h DiffusionSim.def.h DiffusionSim.h ../sim_headers/common_lbsim.h DiffusionNeighbors.C DiffusionMetric.C
	$(CHARMC) -c DiffusionSim.C

DiffusionNeighbors.C: $(CHARM_HOME)/src/ck-ldb/DiffusionNeighbors.C
	cp $< .

DiffusionMetric.C: $(CHARM_HOME)/src/ck-ldb/DiffusionMetric.C
	cp $< .

clean:
	rm -f *.decl.h *.def.h conv-host *.o DiffusionSim DiffusionSim.prj charmrun *~ *.stamp DiffusionNeighbors.C DiffusionMetric.C

