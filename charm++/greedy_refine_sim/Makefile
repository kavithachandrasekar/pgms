-include ../../../common.mk
OPTS	= -g
CHARMC	= ../../../../../mpi-linux-x86_64-smp/bin/charmc $(OPTS)

OBJS = GreedyRefineLB.o

all: GreedyRefine

GreedyRefine: $(OBJS)
	$(CHARMC) -language charm++ -o GreedyRefine $(OBJS)

GreedyRefineLB.decl.h: GreedyRefineLB.ci
	$(CHARMC)  GreedyRefineLB.ci

GreedyRefineLB.o: GreedyRefineLB.C GreedyRefineLB.decl.h
	$(CHARMC) -c GreedyRefineLB.C

clean:
	rm -f *.decl.h *.def.h conv-host *.o GreedyRefine GreedyRefineLB.prj charmrun *~

