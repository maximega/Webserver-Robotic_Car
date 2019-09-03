make all:
	gcc -o webserv webserv.c threads.c -g -w
	gcc -o dynamic/my-histogram my-histogram.c -g -w
