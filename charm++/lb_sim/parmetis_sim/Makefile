OPTS	= -O3
CHARMC = $(CHARM_HOME)/bin/charmc $(OPTS)

OBJS = convert.o

all: convert parmetis

parmetis: parmetis.cpp
	mpicc -std=c++17 parmetis.cpp -I/Users/maya/local/include -I../sim_headers -I/Users/maya/software/charm-diffusionlb/src/util -L/Users/maya/local/lib -lparmetis -lmetis -lgklib -lstdc++ -o parmetis

convert: $(OBJS)
	$(CHARMC) -language charm++ -module CommonLBs -o convert $(OBJS)

convert.o: convert.cpp convert.decl.h
	$(CHARMC) -c convert.cpp

convert.decl.h: convert.ci
	$(CHARMC)  convert.ci

clean:
	rm -f *.decl.h *.def.h conv-host *.o convert charmrun *~
