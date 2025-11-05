-include ../../../common.mk
OPTS	= -O3 -g -std=c++14
CHARMC	= $(CHARM_HOME)/bin/charmc $(OPTS) 

OBJS = DiffusionSim.o

# Files to copy from charm++ source
COPIED_FILES = DiffusionNeighbors.C DiffusionMetric.C DiffusionPseudo.C DiffusionCore.C
COPIED_HEADER = DiffusionJSON.h

all: DiffusionSim

DiffusionSim: $(OBJS)
	$(CHARMC) -language charm++ -o DiffusionSim $(OBJS)

DiffusionSim.def.h DiffusionSim.decl.h: DiffusionSim.ci.stamp

DiffusionSim.ci.stamp: DiffusionSim.ci
	$(CHARMC) $<
	touch $@

DiffusionSim.o: DiffusionSim.C DiffusionSim.decl.h DiffusionSim.def.h DiffusionSim.h ../sim_headers/common_lbsim.h $(COPIED_FILES)
	cp $(CHARM_HOME)/src/ck-ldb/$(COPIED_HEADER) .
	$(CHARMC) -c DiffusionSim.C

# Generic rule to copy files from charm++ source
%.C: $(CHARM_HOME)/src/ck-ldb/%.C
	cp $< .

clean:
	rm -f *.decl.h *.def.h conv-host *.o DiffusionSim DiffusionSim.prj charmrun *~ *.stamp $(COPIED_FILES) $(COPIED_HEADER)

