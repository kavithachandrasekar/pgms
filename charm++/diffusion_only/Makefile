-include ../../../common.mk
OPTS	= -O3 -g
CHARMC	= /u/syellapragada/charm/mpi-linux-x86_64-smp/bin/charmc $(OPTS)

OBJS = Diffusion.o

all: Diffusion

Diffusion: $(OBJS)
	$(CHARMC) -language charm++ -o Diffusion $(OBJS)

Diffusion.decl.h: Diffusion.ci
	$(CHARMC)  Diffusion.ci

Diffusion.o: Diffusion.C Diffusion.decl.h
	$(CHARMC) -c Diffusion.C

clean:
	rm -f *.decl.h *.def.h conv-host *.o Diffusion Diffusion.prj charmrun *~

