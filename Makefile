all:server client

server:server.cpp
	g++ -g -o $@ $< -lpthread

client:client.c
	@gcc -g -o $@ $<

clean:
	@rm -f server client
