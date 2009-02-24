#
# Makefile for SNMP/XML gate
#
# The program needs g++ as compiler
#
#
GCC=g++
LIBS=-lxerces-c
PAR=


all: daemon

daemon: gate.o main.o header/definitions.h
	$(GCC) -o snmpxmld main.o snmpxmlgate.o $(LIBS) 

gate.o: snmpxmlgate.cpp header/snmpxmlgate.h header/definitions.h
	$(GCC) -o snmpxmlgate.o -c snmpxmlgate.cpp  $(PAR)

main.o: main.cpp header/definitions.h
	$(GCC) -o main.o -c main.cpp  $(PAR)



clean:
	rm -f *.o snmpxmld
