#define _XOPEN_SOURCE 500
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <ftw.h>
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define NUM_POINTS 7
#define NUM_COMMANDS 8
#define GNUPLOT "gnuplot -persist"

long int reg_count = 0;
long int dir_count = 0;
long int lnk_count = 0;
long int skt_count = 0;
long int blk_count = 0;
long int chr_count = 0;
long int ffo_count = 0;
long int nopenfd = 800;

/*
 * type_count is called for every file/directory the ftwn encounters
 * type_count increments global counters when its corresponding file type is detected
 */
static int type_count(char *fpath, struct stat *sb, int tflag, struct FTW *ftwbuf){
	
	if (S_ISREG(sb->st_mode)) reg_count++;
	if (S_ISDIR(sb->st_mode)) dir_count++;
	if (S_ISCHR(sb->st_mode)) chr_count++;
	if (S_ISBLK(sb->st_mode)) blk_count++;
	if (S_ISFIFO(sb->st_mode)) ffo_count++;
	if (S_ISLNK(sb->st_mode)) lnk_count++;
	if (S_ISSOCK(sb->st_mode)) skt_count++;

	
	return 0;
}		
int main(int argc, char *argv[]) {
	/*
	 * If the incorrect amount of arguments are entered, respond to server with error code 404
	 */
	if (argc != 2){
		char html_str[] = "HTTP/1.1 404 Not Found\r\nContent-Type: text/plain; charset=UTF-8\r\n\r\nError: no directory argument specified.\n";
		write(1, html_str, strlen(html_str));
		return 1;
	}
	else
		/*
	 	 * If argument is not a valid directory, repond with error code 400
	 	 */
		if (nftw(argv[1], type_count, 400, FTW_DEPTH || FTW_PHYS) == -1) {
        		char html_str[] = "HTTP/1.1 404 Not Found\r\nContent-Type: text/plain; charset=UTF-8\r\n\r\nError: invalid directory argument specified.\n";
			write(1, html_str, strlen(html_str));
			return 1;
		}
		
		/*
	 	 * Y values to be shown on the y-axis of the histogram plot
	 	 */
		long int files[NUM_POINTS] = {reg_count, dir_count, lnk_count, skt_count, blk_count, chr_count, ffo_count};

		/*
	 	 * set up commands for gnu plot to create a histogram jpeg
		 * reads the plot data from a file named data.dat, that will be created later
	 	 */
		char * commandsForGnuplot[NUM_COMMANDS] = {"set title \"File Count\"", "set yrange [0:]","set 									style data histogram", 
								"set xlabel \"File Type\"", 
								"set ylabel \"Count\"",  
								"set terminal png size 750,600", 
								"set output 'histogram.jpeg'", 								"plot 'data.dat' using 2:xtic(1) title 'Files'"};
		/*
	 	 * X values to be shown on the x-axis of the histogram plot
	 	 */
		char * xvals[NUM_POINTS] = {"Regular", "Directory", "Link", "Socket", "Block","Character", "FIFO"};
		FILE * temp = fopen("data.dat", "w");

		/*
		* Opens an interface that sends commands as if they were typing into the gnuplot command line
		* " -persistent" keeps the plot open even after your C program terminates.
		*/
		FILE * gnuplotPipe = popen (GNUPLOT, "w");

		/*
		 * Write the data to a temporary file
		*/
		int i;
		for (i=0; i < NUM_POINTS; i++){
			fprintf(temp, "%s %ld \n", xvals[i], files[i]); 
		}
		/*
		 * Sends commands one by one to gnuplot
		*/
		for (i=0; i < NUM_COMMANDS; i++){
			fprintf(gnuplotPipe, "%s \n", commandsForGnuplot[i]); 
		}

		fflush(gnuplotPipe);

		char html_str[] = "HTTP/1.1 200 OK\r\nContent-Type: text/html; charset=UTF-8\r\n\r\n<!DOCTYPE html>\n<html>\n<head>\n<h1 style=\"color:red; text-align:center; font-size:16pt;\">CS410 Webserver</h1><br />\n</head>\n<body>\n<center><img src=\"../histogram.jpeg\" height=\"600\" width=\"750\"/></center>\n</body>\n</html>";
		/*
		 * Write formatted HTML response back to the server to display the histogram
		 */
		write(1, html_str, strlen(html_str));
		return 1;
}
