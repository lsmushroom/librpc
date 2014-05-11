all:server client
FLAGS=-g -Werror -Wall

server:server.cpp
	g++ $(FLAGS) -o $@ $< -lpthread

client:client.c
	@gcc $(FLAGS) -o $@ $<

clean:
	@rm -f server client
