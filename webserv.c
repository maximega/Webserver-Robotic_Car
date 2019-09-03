#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <limits.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <netdb.h>
#include <fcntl.h>
#include <sys/time.h>
#include "threads.h"

/* Used for parsing HTTP request methods. */
#define GET 0
#define UNKNOWN 1

/*
 * Job struct represent of every incoming request to the server
 * mctx is the machine context for the request
 * job_id is the effective thread_id of the request
 * client_fd is fd to which the server will write its response back to the client
 * pathname is the string entered into the url of the browser by the client
 */
typedef struct job {
	mctx_t* mctx;
	int job_id;
	int client_fd;
	char pathname[];
} job_t;

int config();
int init_server(unsigned short);
int loop();
int spawn_thread(int);
int handle_request(int*);
char * strLower(char *);
char * extCheck(char *);
int cache_lookup();
int cache_insert();
int cache_remove();
void cleanup();

/* Config file defaults */
int MAX_CONNECTIONS = 10;
int RECV_BUF_SIZE = 4096;

unsigned short port_num;
int server_fd;
struct sockaddr_in server_addr;

sig_atomic_t curr_thread;
volatile job_t** job_queue;


/**
* scheduler() handles SIGALRM signals from the itimer and switches to the next thread context that is still active.
**/
void scheduler(int sig) {

	int next_thread = 0, prev_thread = 0;
	int counter = 0;

	do {
		next_thread = (curr_thread + next_thread + 1) % (MAX_CONNECTIONS + 1);

	/* Repeats until an active thread context is found. */
	} while (job_queue[next_thread] == NULL);

	prev_thread = curr_thread;
	curr_thread = next_thread;

/* Save current thread context before switching unless it has exited or we are coming from ourselves. */	
	if (job_queue[prev_thread] == NULL || prev_thread == next_thread) mctx_restore(job_queue[next_thread]->mctx);
	else mctx_switch(job_queue[prev_thread]->mctx, job_queue[next_thread]->mctx, 1); //save sigset for current thread before switching

}

int config() {

	FILE *fd;
	if ((fd = fopen("config", "r")) == NULL) {
		printf("Error: config file could not be found.\n");
		exit(EXIT_FAILURE);
	}

	char linebuf[LINE_MAX];

	while (fgets(linebuf, sizeof(linebuf), fd) != NULL) {

		char *param = strtok(linebuf, "=");
		
		if (strcmp(param, "MAX_CONNECTIONS") == 0)
			MAX_CONNECTIONS = atoi(strtok(NULL, ""));

		else if (strcmp(param, "RECV_BUF_SIZE") == 0)
			RECV_BUF_SIZE = atoi(strtok(NULL, ""));

		else {
			printf("Error: invalid configuration parameter found.  Exiting.\n");
			exit(EXIT_FAILURE);
		}
	}

}
/*initialize server*/
int init_server(unsigned short port) {
	
	/*  Set the server's socket family, type, and port. */
	if ((server_fd = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
		perror("Error");
		exit(EXIT_FAILURE);
	}

	server_addr.sin_family = AF_INET;
	server_addr.sin_addr.s_addr = INADDR_ANY;
	server_addr.sin_port = htons(port);

	int val = 1;
	setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &val, sizeof(int));
	
	/* Bind server to a port. */
	if (bind(server_fd, (struct sockaddr *)&server_addr, sizeof(server_addr)) == -1) {
		perror("Error");
		close(server_fd);
		exit(EXIT_FAILURE);
	}

	/* Prepare server to listen for incoming connections. */
	if (listen(server_fd, MAX_CONNECTIONS) == -1) {
		perror("Error");
		close(server_fd);
		exit(EXIT_FAILURE);
	}

	printf("Server listening.\n");

	return 0;
}

int loop() {

	/* Create context for main thread with thread id 0. */
	mctx_t *mctx = calloc(1, sizeof(mctx_t));;
	mctx->tid = 0;	
	
	job_t *main_thread = calloc(1, sizeof(job_t));
	main_thread->job_id = 0;
	main_thread->mctx = mctx;

	job_queue[0] = main_thread;
	curr_thread = 0;


	/* Set handler for SIGALRM signals to our scheduler() function. 
	   Deliver signal on sigaltstack and restart syscalls. */
	struct sigaction handler_action;

	handler_action.sa_flags = SA_ONSTACK | SA_RESTART;
	sigfillset(&handler_action.sa_mask);
	handler_action.sa_handler = scheduler;

	if (sigaction(SIGALRM, &handler_action, NULL) < 0) {
		perror("Error");
		exit(EXIT_FAILURE);
	}


	/* Create itimer with REAL_TIME interval. */
	struct itimerval it;
	it.it_interval.tv_sec = 0;
	it.it_interval.tv_usec = 0;
	it.it_value = it.it_interval;


	/* Start timer. */
	if (setitimer(ITIMER_REAL, &it, NULL) < 0) {
		perror("Error");
		exit(EXIT_FAILURE);
	}


	/* Listen for new connections forever. */
	while(1) {

		int client_fd;
		struct sockaddr_in client_addr;
		socklen_t sin_len = sizeof(struct sockaddr_in);


		/* If there are no active requests being handled, stop the timer since we don't need to preempt main_thread. */
		int i;
		for (i = 1; i < MAX_CONNECTIONS + 1 && job_queue[i] == NULL; i++) {

			if (i == MAX_CONNECTIONS) {

				it.it_value.tv_sec = 0;
				it.it_value.tv_usec = 0;
			}
		}


		/* accept() will block waiting until a connection comes in. */
		if ((client_fd = accept(server_fd, (struct sockaddr *)&client_addr, &sin_len)) == -1) {
			perror("Error");
			continue;
		}

		
		printf("\n\nSuccessful connection.\n\n");


		/* Turn on itimer in case it was turned off earlier. */
		it.it_value.tv_sec = 5;
		it.it_value.tv_usec = 0;


		/* Spawn a thread to handle new request. */
		spawn_thread(client_fd);
		//shutdown(client_fd, SHUT_WR);
	}

	return 0;
}

/* Send 200 OK response to the specified socket. */
void send200(int client_fd, char type[]) {

	char response_header[] = 
	"HTTP/1.1 200 OK\r\n"
	"Content-Type: ";
	send(client_fd, response_header, strlen(response_header), NULL);
	send(client_fd, type, strlen(type), NULL);

	return;
}

/* Send 404 Not Found response to the specified socket. */
void send404(int client_fd) {

	char response_header[] = 
	"HTTP/1.1 404 Not Found\r\n"
	"Content-Type: text/plain; charset=UTF-8\r\n\r\n"
	"Error 404: File Not Found\n";
	send(client_fd, response_header, strlen(response_header), NULL);

	return;
}

/* Send 501 Not Implemented response to the specified socket. */
void send501(int client_fd) {

	char response_header[] = 
	"HTTP/1.1 501 Not Implemented\r\n"
	"Content-Type: text/plain; charset=UTF-8\r\n\r\n"
	"Error 501: Service Not Implemented\n";
	send(client_fd, response_header, strlen(response_header), NULL);

	return;
}


/** 
* handle_request() is the start function for all threads spawned.
* The function parses HTTP request headers and serves back static or dynamic content.
**/
int handle_request(int *arg) {
	
	char recv_buf[RECV_BUF_SIZE];
	char *linebuf;
	int method;
	char *request_path_raw;


	/* Parse HTTP request and populate job struct. */
	job_t request = *(job_t*)arg;
	int client_fd = request.client_fd;


	/* Read the contents of the request into recv_buf. */
	read(client_fd, recv_buf, sizeof(recv_buf) - 1);


	/* Print request header to server console. */
	printf("%s\n", recv_buf);


	/* Get first line of request header (we discard the rest). */
	linebuf = strtok(recv_buf, "\n");
	char *temp_method = strtok(linebuf, " ");


	/* Check for request method (our server only supports GET). */
	if (strcmp(temp_method, "GET") == 0)
		method = GET;
	else 
		method = UNKNOWN;


	/* Get filepath requested minus HTTP/1.1 from the end of the header. */
	request_path_raw = strtok(NULL, "");
	*(strrchr(request_path_raw, 'H') - 1) = '\0';
	request_path_raw++;


	/* Empty request paths replaced by current directory. */
	if (strlen(request_path_raw) < 1) request_path_raw = ".";


	/* URLdecode request_path_raw and store the decoded verion in request_path. */
	char request_path[strlen(request_path_raw)+1];
	decode(request_path_raw, request_path);
	strncpy(request.pathname, request_path, strlen(request_path));


	/* Check for arguments appended to request_path. */
	char *args = strchr(request_path, '?');
	if (args) {
		*args = '\0';
		args++;
	}


	/* If the method is anything but GET, send 501 response. */
	if (method != GET) {
		send501(client_fd);
		
		/* Cleanup code before exiting thread. */
		free(request.mctx->sk);
		free(request.mctx);
		int temp = request.job_id;
		free(job_queue[request.job_id]);
		job_queue[temp] = NULL;
		shutdown(client_fd, SHUT_WR);

		kill(getpid(), SIGALRM);
	}


	/* If requested file does not exist, send 404 response. */
	struct stat statbuf;
	if (lstat(request_path, &statbuf) < 0)  {

		send404(client_fd);

		/* Cleanup code before exiting thread. */
		free(request.mctx->sk);
		free(request.mctx);
		int temp = request.job_id;
		free(job_queue[request.job_id]);
		job_queue[temp] = NULL;
		shutdown(client_fd, SHUT_WR);

		kill(getpid(), SIGALRM);
	}


	/* If the client requested a directory, execute "ls -l" on the directory, send 200 response. */
	if (S_ISDIR(statbuf.st_mode)) {

		pid_t pid = fork();

		if (pid < 0) {
			perror("Error");
			exit(EXIT_FAILURE);
		}
		else if (pid == 0) {

			send200(client_fd, "text/plain; charset=UTF-8\r\n\r\n");
			dup2(client_fd, 1);
			if (execlp("ls", "ls", "-l", request_path, 0) == -1) {
				perror("Error");
				exit(EXIT_FAILURE);
			}
		}
		else {
			waitpid(pid, NULL, NULL);
			
			/* Cleanup code before exiting thread. */
			free(request.mctx->sk);
			free(request.mctx);
			int temp = request.job_id;
			free(job_queue[request.job_id]);
			job_queue[temp] = NULL;
			shutdown(client_fd, SHUT_WR);

			kill(getpid(), SIGALRM);
		}
	
	}


	/* If the client requested a file, check if it is dynamic/static and respond appropriately. */
	else {
		/* 
		 * Check if request path includes "dynamic/".
		 * All dynamic content needs to be in the dynamic/ directory. 
		 */
		if (strstr(request_path, "dynamic/")) {

			/*
			 * Each argument will be seperated by an "&".
			 * If arguments are present, populate the args buffer.
			 */
			char **args_buf;
			int args_counter = 0;
			if (args && *args) {
				
				args_counter = 1;
				int i;
				for (i = 0; args[i] != NULL; i++) {
					if (args[i] == '&')
						args_counter += 1;
				}

				args_buf = calloc(args_counter + 2, sizeof(char*));

				args_buf[0] = strtok(args, "&");
				int j;
				for (j = 1; j < args_counter; j++) {
					args_buf[j] = strtok(NULL, "&");
				}
			}

			
			/* Default method to run dynamic content is simple fork/exec. */
			char *prog_to_exec = request_path;


			/*
			   Get file extension if it exists.
			   Update prog_to_exec accordingly.
			*/
			char *ext = strrchr(request_path, '.');
			if(ext && *ext){
				ext++;
				ext = strLower(ext);
	
				if (strcmp(ext, "py") == 0)
					prog_to_exec = "python";
				else{
					prog_to_exec = "perl";
				}
			}
			
			/* 
			 * Create an array of char* to copy in exec information (process, arguments).
			 * exec_args[0] will contain the name of the program to execute.
			 * exec_args[1] will contain the name of the requested path (excluding arguments).
			 * Arguments will be added to the array after the first 2 elements are set
			 */
			char **exec_args = calloc(args_counter + 3, sizeof(char*));
			exec_args[0] = prog_to_exec;
			exec_args[1] = request_path;

			int i;
			for (i = 0; i < args_counter; i++) {

				exec_args[i+2] = args_buf[i];
			}
			exec_args[args_counter + 3 - 1] = NULL;


			/* Fork */
			pid_t pid = fork();

			if (pid < 0) {
				perror("Error");
				exit(EXIT_FAILURE);
			}


			/* Child */
			else if (pid == 0) {
				
				/* All output from dynamic files will go into a file named dynamic/response. */
				int response_fd = open("dynamic/response", O_WRONLY | O_TRUNC);
				dup2(response_fd, 1);

				/* Depending on if we are execing a binary or using python/perl, change how many args we send. */
				if (ext)
					execvp(exec_args[0], exec_args);

				else
					execvp(exec_args[0], &exec_args[1]);

				exit(EXIT_FAILURE);
			}


			/* Parent */
			else {

				waitpid(pid, NULL, NULL);
				int response_fd;
				
				/* server reads from dynamic/response for the client output that was sent */
				struct stat response_statbuf;
				if (lstat("dynamic/response", &response_statbuf) < 0)  {
					perror("Error");
					cleanup();
					return;
				}
				
				response_fd = open("dynamic/response", O_RDONLY);
				sendfile(client_fd, response_fd, 0, response_statbuf.st_size);
				close(response_fd);
			}
		}


		/* Default to static content. */
		else {

			char *ext = strrchr(request_path, '.');
			char *lower;
			char *type = "text/plain; charset=UTF-8\r\n\r\n";

			/* Get MIME type for file. */
			if(ext){
				ext++;
				lower = strLower(ext);
				type = extCheck(ext);
			}


			/* Send 200 response header followed by contents of file. */
			int resource_fd = open(request_path, O_RDONLY);
			send200(client_fd, type);
			sendfile(client_fd, resource_fd, 0, statbuf.st_size * 2);
		}


		/* Cleanup code before exiting thread. */
		free(request.mctx->sk);
		free(request.mctx);
		int temp = request.job_id;
		free(job_queue[request.job_id]);
		job_queue[temp] = NULL;
		shutdown(client_fd, SHUT_WR);

		kill(getpid(), SIGALRM);
	}
}


/* 
 * URLdecoder function to decode requests that include special characters.
 * Source: https://www.rosettacode.org/wiki/URL_decoding#C
 */ 
int decode(char *str, char *decoded){
	char *o;
	char *end = str + strlen(str);
	int chr;
 
	for (o = decoded; str <= end; o++) {
		chr = *str++;
		if (chr == '+') chr = ' ';
		else if (chr == '%' && (!isHex(*str++)	||
					!isHex(*str++)	||
					!sscanf(str - 2, "%2x", &chr)))
			return -1;
 
		if (decoded) *o = chr;
	}
 
	return o - decoded;
}


/* Hex checker used in URLdecoder. */
inline int isHex(int x){
	return	(x >= '0' && x <= '9')	||
		(x >= 'a' && x <= 'f')	||
		(x >= 'A' && x <= 'F');
}


/* Sets all character in a string to lower case. */
char * strLower(char * str){
	char *p;
	for (p = str; *p != '\0'; p++){
		*p = tolower(*p);
	}
	return str;
}


/* Get the MIME type of files requested by the client. */
char * extCheck(char * ext){
	
	if(strcmp(ext, "html") == 0 || strcmp(ext, "htm") == 0)
		return "text/html; charset=UTF-8\r\n\r\n";
	if (strcmp(ext, "css") == 0)
		return "text/css; charset=UTF-8\r\n\r\n";
	if (strcmp(ext, "xml") == 0)
		return "text/xml; charset=UTF-8\r\n\r\n";
	if (strcmp(ext, "txt") == 0)
		return "text/plain; charset=UTF-8\r\n\r\n";
	if (strcmp(ext, "py") == 0)
		return "text/plain; charset=UTF-8\r\n\r\n";
	if (strcmp(ext, "cgi") == 0)
		return "text/plain; charset=UTF-8\r\n\r\n";
	if (strcmp(ext, "java") == 0)
		return "text/plain; charset=UTF-8\r\n\r\n";
	if (strcmp(ext, "c") == 0 || strcmp(ext, "h") == 0 || strcmp(ext, "cpp") == 0)
		return "text/plain; charset=UTF-8\r\n\r\n";
	if(strcmp(ext, "jpeg") == 0 || strcmp(ext, "jpg") == 0)
		return "image/jpg\r\n\r\n";
	if(strcmp(ext, "png") == 0)
		return "image/png\r\n\r\n";
	if(strcmp(ext, "gif") == 0 )
		return "image/gif\r\n\r\n";
	if (strcmp(ext, "js") == 0)
		return "application/javascript\r\n\r\n";
	if (strcmp(ext, "pdf") == 0)
		return "application/pdf\r\n\r\n";
	if (strcmp(ext, "json") == 0)
		return "application/json\r\n\r\n";
	if (strcmp(ext, "mp4") == 0)
		return "video/mp4\r\n\r\n";
	if (strcmp(ext, "mov") == 0)
		return "video/quicktime\r\n\r\n";
	else
		return "application/octet-stream\r\n\r\n";
}

/* Create new job and machine context using sigaltstack method. */
int spawn_thread(int client_fd) {

	/* Find an open slot in the global job queue to store the incoming request. */
	int i = 0;
	while (i < MAX_CONNECTIONS + 1 && job_queue[i] != NULL) {
		i++;
	}

	
	/* If there is no space in the queue, drop the request.
	   Note: This should never happen as the server shouldn't accept more than MAX_CONNECTIONS. */
	if (i >= MAX_CONNECTIONS + 1) {
		printf("Error spawning thread.\n");
		return 1;
	}


	/* Create space to store a machine context. */
	mctx_t *mctx = malloc(sizeof(mctx_t));


	/* Create a stack of 8192 bytes that can grow up or down. */
	size_t stack_sz = 8192;
	void *stack = calloc(2, stack_sz);
	mctx->sk = stack; //store start of calloc'd buffer


	/* Store relevant attributes for each job request. */
	job_t *job = calloc(1, sizeof(job_t));
	job->job_id = i;
	job->client_fd = client_fd;
	job->mctx = mctx;
	

	/* Add job to global queue and create machine context. */
	job_queue[i] = job;
	mctx_create(mctx, handle_request, job, stack+stack_sz, stack_sz);


	/* Send SIGALRM to current process to initiate context switching. */
	kill(getpid(), SIGALRM);

	return 0;
}

/* cleanup() free's any outstanding heap memory. */
void cleanup() {

	close(server_fd);

	int i;
	for (i = 0; i < sizeof(job_queue); i++) {

		if (job_queue[i]) {

			if (job_queue[i]->mctx) {


				if (job_queue[i]->mctx->sk) free(job_queue[i]->mctx->sk);
				free(job_queue[i]->mctx);
			}

			free(job_queue[i]);
			job_queue[i] = NULL;
		}

	}

	return;
}

int main(int argc, char *argv[]) {

	/* Call cleanup() on process exit. */
	atexit(cleanup);


	/* Ensure server is invoked with only 1 argument. */
	if (argc != 2) {
		printf("Usage: ./webserv <port>\n");
		exit(EXIT_FAILURE);
	}


	/* Set port number and configuration variables from config file. */
	port_num = atoi(argv[1]);
	config();


	/* Initialize queue for incoming requests. */
	job_queue = calloc(MAX_CONNECTIONS + 1, sizeof(mctx_t*));


	/* Initialize server. */
	init_server(port_num);


	/* Listens for requests forever. */ 
	loop();
}
