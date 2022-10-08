CC = g++
CFLAGS = -Wall

main:
	g++ client-phase1.cpp -o exec-client-phase1
	g++ client-phase2.cpp -o exec-client-phase2
	g++ client-phase3.cpp -o exec-client-phase3 -lcrypto
	g++ client-phase4.cpp -o exec-client-phase4
	g++ client-phase5.cpp -o exec-client-phase5 -lcrypto