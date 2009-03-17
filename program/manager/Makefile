#
# Makefile for SNMP/XML gate
#
# The program needs g++ as compiler
#
#
GCC=g++
LIBS=-lcurl
PAR=-Wall


all: daemon

daemon: main.o
	$(GCC) -o manager main.o $(LIBS) 

main.o: main.cpp
	$(GCC) -o main.o -c main.cpp  $(PAR)


clean:
	rm -f *.o manager
