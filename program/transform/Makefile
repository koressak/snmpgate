#
# Makefile for SNMP/XML gate
#
# The program needs g++ as compiler
#
#
GCC=g++
LIBS=-lxerces-c -lsnmp
PAR=-Wall


all: daemon

daemon: main.o
	$(GCC) -o transform main.o $(LIBS) 

main.o: main.cpp
	$(GCC) -o main.o -c main.cpp  $(PAR)


clean:
	rm -f *.o transform
