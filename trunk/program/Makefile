#
# Makefile for SNMP/XML gate
#
# The program needs g++ as compiler
#
#
GCC=g++
LIBS=-lxerces-c -lsnmp
PAR=


all: daemon

daemon: gate.o snmpmod.o main.o header/definitions.h
	$(GCC) -o snmpxmld main.o snmpxmlgate.o snmpmod.o $(LIBS) 

snmpmod.o: header/snmpmod.h header/definitions.h snmpmod.cpp
	$(GCC) -o snmpmod.o -c snmpmod.cpp  $(PAR)

gate.o: snmpxmlgate.cpp header/snmpxmlgate.h header/definitions.h
	$(GCC) -o snmpxmlgate.o -c snmpxmlgate.cpp  $(PAR)

main.o: main.cpp header/definitions.h
	$(GCC) -o main.o -c main.cpp  $(PAR)


clean:
	rm -f *.o snmpxmld
