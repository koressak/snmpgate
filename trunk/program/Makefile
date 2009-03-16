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

daemon: gate.o snmpmod.o main.o mib2xsd.o header/definitions.h
	$(GCC) -o snmpxmld main.o snmpxmlgate.o snmpmod.o mib2xsd.o $(LIBS) 

snmpmod.o: snmpmod.cpp header/snmpmod.h header/definitions.h
	$(GCC) -o snmpmod.o -c snmpmod.cpp  $(PAR)

gate.o: snmpxmlgate.cpp header/snmpxmlgate.h header/definitions.h
	$(GCC) -o snmpxmlgate.o -c snmpxmlgate.cpp  $(PAR)

mib2xsd.o: mib2xsd.cpp header/mib2xsd.h  header/definitions.h
	$(GCC) -o mib2xsd.o -c mib2xsd.cpp  $(PAR)

main.o: main.cpp header/definitions.h
	$(GCC) -o main.o -c main.cpp  $(PAR)


clean:
	rm -f *.o snmpxmld
