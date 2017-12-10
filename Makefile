SRCDIR   = src
OBJDIR   = obj
BINDIR   = bin

all: dme_nc dme_bm

$(OBJDIR)/simple.o: $(SRCDIR)/simple.c $(SRCDIR)/dme.h
	gcc -c $(SRCDIR)/simple.c -o $(OBJDIR)/simple.o

$(BINDIR)/nc: $(SRCDIR)/node_controller.c $(SRCDIR)/dme.h $(OBJDIR)/simple.o
	gcc $(SRCDIR)/node_controller.c $(OBJDIR)/simple.o -o $(BINDIR)/nc -lpthread

$(BINDIR)/prod: $(SRCDIR)/producer.c $(SRCDIR)/dme.h $(OBJDIR)/simple.o
	gcc $(SRCDIR)/producer.c $(OBJDIR)/simple.o -o $(BINDIR)/prod

$(BINDIR)/bm: $(SRCDIR)/buffer_manager.c
	gcc $(SRCDIR)/buffer_manager.c -o $(BINDIR)/bm -lpthread

dme_nc: node_controller.df $(BINDIR)/nc $(BINDIR)/prod
	docker build -t dme_nc -f node_controller.df .

dme_bm: node_controller.df $(BINDIR)/bm
	docker build -t dme_bm -f buffer_manager.df .

clean:
	rm -f $(BINDIR)/*
