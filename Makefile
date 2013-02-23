all : dh_retty client

dh_retty: dh_retty.c
	gcc -lutil -g -o dh_retty dh_retty.c
client: client.c
	gcc -g -o client client.c
clean:
	rm -f dh_retty client
