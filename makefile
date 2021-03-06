.POSIX:
CC= gcc
CFLAGS= -g -Wall -Werror
HEADERS= cir.h cir_internal.h
OBJECTS= \
		CirArray.o \
		CirAttr.o \
		CirBBuf.o \
		CirBuiltin.o \
		CirCode.o \
		CirComp.o \
		CirDl.o \
		CirEnum.o \
		CirEnumItem.o \
		CirEnv.o \
		CirFkind.o \
		CirHash.o \
		CirIkind.o \
		CirLex.o \
		CirLog.o \
		CirLoopEnv.o \
		CirMachine.o \
		CirMem.o \
		CirName.o \
		CirParse.o \
		CirPrime.o \
		CirQuote.o \
		CirRender.o \
		CirStmt.o \
		CirType.o \
		CirTypedef.o \
		CirValue.o \
		CirVar.o \
		CirX64.o \
		main.o

all: cir
cir: $(OBJECTS)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $(OBJECTS) -ldl

CirArray.o: CirArray.c $(HEADERS)
CirAttr.o: CirAttr.c $(HEADERS)
CirBBuf.o: CirBBuf.c $(HEADERS)
CirBuiltin.o: CirBuiltin.c $(HEADERS)
CirCode.o: CirCode.c $(HEADERS)
CirComp.o: CirComp.c $(HEADERS)
CirDl.o: CirDl.c $(HEADERS)
CirEnum.o: CirEnum.c $(HEADERS)
CirEnumItem.o: CirEnumItem.c $(HEADERS)
CirEnv.o: CirEnv.c $(HEADERS)
CirFkind.o: CirFkind.c $(HEADERS)
CirHash.o: CirHash.c $(HEADERS)
CirIkind.o: CirIkind.c $(HEADERS)
CirLex.o: CirLex.c $(HEADERS)
CirLog.o: CirLog.c $(HEADERS)
CirLoopEnv.o: CirLoopEnv.c $(HEADERS)
CirMachine.o: CirMachine.c $(HEADERS)
CirMem.o: CirMem.c $(HEADERS)
CirName.o: CirName.c $(HEADERS)
CirParse.o: CirParse.c $(HEADERS)
CirPrime.o: CirPrime.c $(HEADERS)
CirQuote.o: CirQuote.c $(HEADERS)
CirRender.o: CirRender.c $(HEADERS)
CirStmt.o: CirStmt.c $(HEADERS)
CirType.o: CirType.c $(HEADERS)
CirTypedef.o: CirTypedef.c $(HEADERS)
CirValue.o: CirValue.c $(HEADERS)
CirVar.o: CirVar.c $(HEADERS)
CirX64.o: CirX64.c $(HEADERS)
main.o: main.c $(HEADERS)

clean:
	rm -f cir $(OBJECTS)
