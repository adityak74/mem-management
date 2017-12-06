# Makefile for compiling oss and user program
# Aditya Karnam
# Dec, 2017
# Added extra function to clen .log and .out files for easy testing

CC	= gcc
TARGETS	= oss user 
OBJS	= oss.o user.o
SRCDIR  = src
HEADER = shm_header.h
LDFLAGS = -pthread -lpthread

all: $(TARGETS)

$(TARGETS): % : %.o
		$(CC) -o $@ $< $(LDFLAGS)

$(OBJS) : %.o : $(SRCDIR)/%.c
		$(CC) -c $< $(LDFLAGS)

clean:
		/bin/rm -f *.o $(TARGETS) *.log *.out

cleanobj: 
		/bin/rm -f *.o
