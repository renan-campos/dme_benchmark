SRCDIR   = src
OBJDIR   = lib
BINDIR   = bin

all: $(OBJDIR)/fuchi.so $(OBJDIR)/maekawa.so $(OBJDIR)/ricart.so $(OBJDIR)/simple.so dme_nc dme_bm

$(OBJDIR)/fuchi.so: $(SRCDIR)/fuchi.c $(SRCDIR)/dme.h
	gcc -shared -o $(OBJDIR)/fuchi.so $(SRCDIR)/fuchi.c -fPIC

$(OBJDIR)/maekawa.so: $(SRCDIR)/maekawa.c $(SRCDIR)/dme.h
	gcc -shared -o $(OBJDIR)/maekawa.so $(SRCDIR)/maekawa.c -fPIC

$(OBJDIR)/ricart.so: $(SRCDIR)/ricart.c $(SRCDIR)/dme.h
	gcc -shared -o $(OBJDIR)/ricart.so $(SRCDIR)/ricart.c -fPIC

# This is an example shared distributed mutual exclusion library
$(OBJDIR)/simple.so: $(SRCDIR)/simple.c $(SRCDIR)/dme.h
	gcc -shared -o $(OBJDIR)/simple.so $(SRCDIR)/simple.c -fPIC

$(BINDIR)/nc: $(SRCDIR)/node_controller.c $(SRCDIR)/dme.h
	gcc $(SRCDIR)/node_controller.c -o $(BINDIR)/nc -ldl -lpthread

$(BINDIR)/prod: $(SRCDIR)/producer.c $(SRCDIR)/dme.h
	gcc $(SRCDIR)/producer.c -o $(BINDIR)/prod -ldl

$(BINDIR)/bm: $(SRCDIR)/buffer_manager.c
	gcc $(SRCDIR)/buffer_manager.c -o $(BINDIR)/bm -lpthread

dme_nc: node_controller.df $(BINDIR)/nc $(BINDIR)/prod
	docker build -t dme_nc -f node_controller.df .

dme_bm: node_controller.df $(BINDIR)/bm
	docker build -t dme_bm -f buffer_manager.df .

clean:
	rm -vf $(BINDIR)/* $(OBJDIR)/*
