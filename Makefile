SRCDIR   = src
OBJDIR   = obj
BINDIR   = bin

all: dist_mut_exc

$(OBJDIR)/simple.o: $(SRCDIR)/simple.c $(SRCDIR)/dme.h
	gcc -c $(SRCDIR)/simple.c -o $(OBJDIR)/simple.o

$(BINDIR)/nc: $(SRCDIR)/node_controller.c $(SRCDIR)/dme.h $(OBJDIR)/simple.o
	gcc $(SRCDIR)/node_controller.c $(OBJDIR)/simple.o -o $(BINDIR)/nc -lpthread

$(BINDIR)/con: $(SRCDIR)/simple_consumer.c $(SRCDIR)/dme.h $(OBJDIR)/simple.o
	gcc $(SRCDIR)/simple_consumer.c $(OBJDIR)/simple.o -o $(BINDIR)/con -lpthread


dist_mut_exc: Dockerfile $(BINDIR)/nc $(BINDIR)/con
	docker build -t dist_mut_exc .

clean:
	rm -f $(BINDIR)/*
