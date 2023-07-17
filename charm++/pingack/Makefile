-include ../../common.mk
SRC=../../../src
CHARMC=../../../bin/charmc $(OPTS)

OBJS = pingack.o
OBJS_FIXED = pingack_fixedmsg.o

all:	pgm

pgm: $(OBJS)
	$(CHARMC) -language charm++ -o pgm $(OBJS)

pgm_fixed: $(OBJS_FIXED)
	$(CHARMC) -tracemode projections -language charm++ -o pgm_fixed $(OBJS_FIXED)

pgm.prj: $(OBJS)
	$(CHARMC) -tracemode projections -language charm++ -o pgm.prj $(OBJS)

cifiles: pingack.ci
	$(CHARMC)  pingack.ci
	touch cifiles

cifiles_f: pingack_fixedmsg.ci
	$(CHARMC)  pingack_fixedmsg.ci
	touch cifiles_f

clean:
	rm -f *.decl.h *.def.h conv-host *.o pgm pgm_fixed charmrun cifiles cifiles_f pgm.prj pgm.exe pgm.pdb pgm.ilk

pingack.o: pingack.C cifiles
	$(CHARMC) -I$(SRC)/conv-core pingack.C

pingack_fixedmsg.o: pingack_fixedmsg.C cifiles_f
	$(CHARMC) -I$(SRC)/conv-core pingack_fixedmsg.C

test: all
	@echo "Intra-processor Pingpong.."
	$(call run, ./pgm +p1 )
	@echo "Inter-processor Pingpong.."
	$(call run, ./pgm +p2 )

bgtest: all
	@echo "Intra-processor Pingpong.."
	$(call run, ./pgm +p1 +x1 +y1 +z1 )
	@echo "Inter-processor Pingpong.."
	$(call run, ./pgm +p2 +x2 +y1 +z1 )

