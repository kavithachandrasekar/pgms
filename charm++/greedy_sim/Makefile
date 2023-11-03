-include ../../../common.mk
OPTS	= -g
CHARMC	= ../../../../../bin/charmc $(OPTS)

OBJS = Greedy0.o

all: Greedy0

Greedy0: $(OBJS)
	$(CHARMC) -language charm++ -o Greedy0 $(OBJS)

Greedy0.decl.h: Greedy0.ci
	$(CHARMC)  Greedy0.ci

Greedy0.o: Greedy0.C Greedy0.decl.h
	$(CHARMC) -c Greedy0.C

clean:
	rm -f *.decl.h *.def.h conv-host *.o Greedy0 Greedy0.prj charmrun *~

