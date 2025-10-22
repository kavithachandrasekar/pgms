-include ../../../common.mk
OPTS	= -g -I$(CHARM_HOME)/src/libs/ck-libs/metis/libmetis
CHARMC	= $(CHARM_HOME)/bin/charmc $(OPTS)

OBJS = MetisLB.o

all: Metis

Metis: $(OBJS)
	$(CHARMC) -language charm++ -o Metis $(OBJS) -lckmetis

MetisLB.decl.h: MetisLB.ci
	$(CHARMC)  MetisLB.ci

MetisLB.o: MetisLB.C MetisLB.decl.h
	$(CHARMC) -c MetisLB.C

clean:
	rm -f *.decl.h *.def.h conv-host *.o Metis MetisLB.prj charmrun *~

