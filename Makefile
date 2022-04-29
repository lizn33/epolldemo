CC = g++

LDFLAGS = -lpthread 

all: epolldemo

epolldemo: epolldemo.o
	$(CC) $< $(LDFLAGS) -o $@

clean: 
	rm *.o