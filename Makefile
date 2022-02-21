all: peer ds

peer: peer.o
	gcc -Wall peer.o -o peer
	
ds: ds.o
	gcc -Wall ds.o -o ds
	
clean:
	rm *.o peer ds