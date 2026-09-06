# Rohit Ranjeet Satpute
# 23CS10060

all: smserver smclient

smserver: smserver.c smserver.h
	gcc -Wall -o smserver smserver.c

smclient: smclient.c smclient.h
	gcc -Wall -o smclient smclient.c

clean:
	rm -f smserver smclient 
#depends on the requirement mailboxes/* can be added in make clean but generally we don't delete mails

