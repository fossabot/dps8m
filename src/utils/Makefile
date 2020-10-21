include ../Makefile.mk

all : prt2pdf$(EXE)

prt2pdf$(EXE) : prt2pdf.o
	@echo LD prt2pdf$(EXE)
	@$(LD) $(LDFLAGS) -o prt2pdf$(EXE) prt2pdf.o 

install : prt2pdf$(EXE)
ifeq ($(UNAME_S), NetBSD)
	install -d $(INSTALL_BIN)
endif
	cp prt2pdf$(EXE) $(INSTALL_BIN)/prt2pdf$(EXE)

clean :
	-rm prt2pdf$(EXE) prt2pdf.o
