SRCDIR   = src
OBJDIR   = obj
BINDIR   = bin

all: dist_mut_exc

$(BINDIR)/nc: $(SRCDIR)/node_controller.c
	gcc $(SRCDIR)/node_controller.c -o $(BINDIR)/nc


dist_mut_exc: Dockerfile $(BINDIR)/nc
	docker build -t dist_mut_exc .

clean:
	rm -f $(BINDIR)/*
