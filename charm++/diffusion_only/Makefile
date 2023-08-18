-include ../../../common.mk
OPTS	= -O3
CHARMC	= ../../../../bin/charmc $(OPTS)

OBJS = DiffusionLB.o

all: DiffusionLB

DiffusionLB: $(OBJS)
	$(CHARMC) -language charm++ -o DiffusionLB $(OBJS)

DiffusionLB.decl.h: DiffusionLB.ci
	$(CHARMC)  DiffusionLB.ci

DiffusionLB.o: DiffusionLB.C DiffusionLB.decl.h
	$(CHARMC) -c DiffusionLB.C

clean:
	rm -f *.decl.h *.def.h conv-host *.o DiffusionLB DiffusionLB.prj charmrun *~

