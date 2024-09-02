OPTS	= -O3
CHARMC ?= /u/syellapragada/charm/mpi-linux-x86_64-smp/bin/charmc $(OPTS)

OBJS = stencil3d.o

all: stencil3d

stencil3d: $(OBJS)
	$(CHARMC) -language charm++ -module CommonLBs -o stencil3d $(OBJS)

projections: $(OBJS)
	$(CHARMC) -language charm++ -module CommonLBs -tracemode projections -lz -o stencil3d.prj $(OBJS)

summary: $(OBJS)
	$(CHARMC) -language charm++ -module CommonLBs -tracemode summary -lz -o stencil3d.sum $(OBJS)

stencil3d.decl.h: stencil3d.ci
	$(CHARMC)  stencil3d.ci

stencil3d.o: stencil3d.C stencil3d.decl.h
	$(CHARMC) -c stencil3d.C

clean:
	rm -f *.decl.h *.def.h conv-host *.o stencil3d stencil3d.prj charmrun *~

test: stencil3d
	./charmrun +p4 ++ppn 2 ./stencil3d 64 32 +balancer TreeLB +TreeLBFile pe_process_root.json +LBDebug 1
