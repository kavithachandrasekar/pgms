-include ../../../common.mk
OPTS	= -O3 -g
CHARMC	= ../../../../../mpi-linux-x86_64-smp/bin/charmc $(OPTS)

OBJS = Diffusion.o

all: Diffusion

Diffusion: $(OBJS)
	$(CHARMC) -language charm++ -o Diffusion $(OBJS)

Diffusion.def.h Diffusion.decl.h: Diffusion.ci.stamp

Diffusion.ci.stamp: Diffusion.ci
	$(CHARMC) $<
	touch $@

Diffusion.o: Diffusion.C Diffusion.decl.h Diffusion.def.h Diffusion.h ../sim_headers/common_lbsim.h
	$(CHARMC) -c Diffusion.C

clean:
	rm -f *.decl.h *.def.h conv-host *.o Diffusion Diffusion.prj charmrun *~ *.stamp

