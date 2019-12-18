/*
Jacob Pugsley
CS 360
April 28, 2019
*/

#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <errno.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <sys/wait.h>
#include <ctype.h>
#include <limits.h>
#include "mftp.h"

#define PORT 49999

int waitForResponse( int fd, char* buffer, int maxChars ){
	//read server responses until a newline is encountered
	//  or the buffer size is exceeded	
	memset(buffer, 0, maxChars);
	int rd = -1;
	char ch;
	int bp = 0;
	while((rd = read(fd, &ch, 1)) > 0 && bp < maxChars){
		if( ch == 10 ){
			return bp;
		}
		buffer[bp] = ch;
		bp++;
	}
	return -1;
}

int createDataConnection(int fd, char* hostName){
	//open the server data connection
	char ack[256];
	write(fd, "D\n", 2);
	int err = waitForResponse(fd, ack, 256);
	if( err == -1 ){
		write(fd, "Q\n", 2);
		waitForResponse(fd, ack, 256);
		return 1;
	}else if( err == 1 ){
		err = waitForResponse(fd, ack, 256);
	}
	if(strncmp(ack, "E", 1) == 0){
		printf("%s\n", ack);
		return -1;
	}

	//get the port number from the acknowledgment
	char prt[6];
	for( int i = 1; i < err; i++ ){
		prt[i-1] = ack[i];
	}

	//should return a file descriptor for the data connection
	int dfd = socket( AF_INET, SOCK_STREAM, 0 );
	if( dfd < 0 ){
		fprintf(stderr, "Error: socket reports %s\n", strerror(errno) );
		return -1;
	}

	struct sockaddr_in servAddr;
	struct hostent* hostEntry;
	struct in_addr **pptr;
	memset( &servAddr, 0, sizeof(servAddr) );
	servAddr.sin_family = AF_INET;
	servAddr.sin_port = htons(atoi(prt));

	struct sockaddr_in sockAdd;
	memset(&sockAdd, 0, sizeof(sockAdd));
	unsigned int lengthOfSockAdd = sizeof(sockAdd);
	if( getsockname( dfd, (struct sockaddr*) &sockAdd, &lengthOfSockAdd) < 0 ){
		perror("getsockname");
		return -1;
	}

	hostEntry = gethostbyname(hostName);
	if( !hostEntry ){
		herror("Error: gethostbyname reports");
		return -1;
	}

	pptr = (struct in_addr**) hostEntry->h_addr_list;
	memcpy(&servAddr.sin_addr, *pptr, sizeof(struct in_addr) );



	//connect to the server
	err = connect(dfd, (struct sockaddr*) &servAddr, sizeof(servAddr));
	if( err != 0 ){
		fprintf(stderr, "Error: connect reports %s\n", strerror(errno));
		return -1;
	}
	return dfd;
}

int ls(){
	//fork and exec ls -l, using more to display
	int pipefd[2]; 
	if( pipe(pipefd) != 0 ){
		fprintf(stderr, "Client Error: pipe in ls reports %s\n", strerror(errno));
		return 1;
	}
	//exec ls
	int pid = fork();
	if( pid == 0 ){
		//child
		close(pipefd[0]); 
		close( 1 );      
		dup( pipefd[1] );
		close(pipefd[1]);
		execl( "/bin/ls", "ls", "-l", (char*)0);
		fprintf(stderr, "Error: exec reports %s\n", strerror(errno));
		exit(1);
	}else if( pid < 0 ){
		//error
		fprintf(stderr, "Client Error: fork in ls reports %s\n", strerror(errno) );
		return 1;
	}else{
		//parent
		wait(NULL);
		close( pipefd[1] );
		
		//exec more
		pid = fork();
		if( pid == 0 ){
			//child
			close( 0 );
			dup( pipefd[0] );
			close(pipefd[0]);
			execl( "/bin/more", "more", "-20", (char*)0);
			fprintf(stderr, "Error: exec reports %s\n", strerror(errno));
			return 1;
		}else if( pid < 0 ){
			fprintf(stderr, "Client Error: fork in ls reports %s\n", strerror(errno) );
			return 1;
		}else{
			//parent
			wait(NULL);
			close(pipefd[0]);
			close(pipefd[1]);
		}
	}
	return 0;
}

void fileNameFromPath( char* path, char* fname ){
	//get the last part of a file path
	//  and store it in fname
	int i = strlen(path)-1;
	while( i > 1 && path[i] != 47){
		i--;
	}
	int j = 0;
	while( path[i] != 10 ){
		fname[j] = path[i];
		j++;
		i++;
	}
	strcat(fname, "\0");
}

int cmdLoop( int fd, char* hostName ){
	if( fd < 0 ){
		fprintf(stderr, "Error: cmdLoop receieved bad FD\n");
		return 1;
	}
	char cmd[256]; //holds client commands
	char ack[256]; //holds server responses
	while( 1 ){
		memset(cmd, 0, 256);		
		printf("MFTP>");
		fflush(stdout);
		fgets(cmd, 256, stdin);
		//get individual params from the command
		char* word;
		char* args[256];
		int w = 0;
		word = strtok(cmd, " ");
		while( word != NULL ){
			args[w] = word;
			w++;
			word = strtok(NULL, " ");
		}

		//figure out what the command is
		if( strncmp(args[0], "exit", 4) == 0 ){
			write(fd, "Q\n", 2);
			int err = waitForResponse(fd, ack, 256);
			if( err == -1 ){
				return 1;
			}
			return 0;
		}else if( strncmp(args[0], "cd", 2) == 0 ){
			//remove extra space from argument
			char p[PATH_MAX] = "";
			snprintf(p, strlen(args[1]), "%s", args[1]);
			
			if( chdir( p ) != 0 ){
				fprintf(stderr, "Error: chdir reports %s\n", strerror(errno) );
			}
		}else if( strncmp(args[0], "rcd", 3) == 0 ){
			//change server directory
			char sc[PATH_MAX+2];
			snprintf(sc, strlen(args[1])+2, "C%s\n", args[1]);
			write( fd, sc, strlen(sc) );
			
			int err = waitForResponse(fd, ack, 256);
			if( err == -1 ){
				write(fd, "Q\n", 2);
				waitForResponse(fd, ack, 256);
				return 1;
			}
			if(strncmp(ack, "E", 1) == 0){
				printf("%s\n", ack);
				continue;
			}
		}else if( strncmp(args[0], "ls", 2) == 0 ){
			int err = ls();
			if( err != 0 ){
				return 1;
			}
		}else if( strncmp(args[0], "show", 4) == 0 ){
			//print the contents of a remote file with more
			int dc = createDataConnection(fd, hostName);
			if( dc == -1 ){
				write(fd, "Q\n", 2);
				waitForResponse(fd, ack, 256);
				return 1;
			}
			
			//get the file
			char sc[PATH_MAX+2];
			snprintf(sc, strlen(args[1])+2, "G%s\n", args[1]); 
			write(fd, sc, strlen(sc));
			int err = waitForResponse(fd, ack, 256);
			if( err == -1 ){
				write(fd, "Q\n", 2);
				waitForResponse(fd, ack, 256);
				return 1;
			}
			if( strncmp(ack, "E", 1) == 0 ){
				printf("%s\n", ack);
				continue;
			}
			
			//exec more on the data sent back from the server
			int pid = fork();
			if(pid == 0){
				//child
				close(0);
				dup(dc); 
				close(dc);
				execl("/bin/more", "more", "-20", (char*)0);
				fprintf(stderr, "Error: exec reports %s\n", strerror(errno));
				close(dc);				
				return 1;
			}else if( pid < 0 ){
				fprintf(stderr, "Error: fork reports %s\n", strerror(errno));
				write(fd, "Q\n", 2);
				close(dc);				
				return 1;
			}else{
				//parent
				wait(NULL);
			}
			close(dc);
		}else if( strncmp(args[0], "rls", 3) == 0 ){
			//list directory on the server
			int dc = createDataConnection(fd, hostName);
			if( dc == -1 ){
				write(fd, "Q\n", 2);
				waitForResponse(fd, ack, 256);
				return 1;
			}
			
			write(fd, "L\n", 2);

			//exec more on the data sent back from the server
			int pid = fork();
			if( pid == 0 ){
				//child
				close(0);
				dup(dc);
				close(dc);
				execl("/bin/more", "more", "-20", (char*)0);
			}else if( pid < 0 ){
				fprintf(stderr, "Error: fork reports %s\n", strerror(errno));
				write(fd, "Q\n", 2);
				close(dc);
				return 1;
			}else{
				//parent
				wait(NULL);
			}
			close(dc);
		}else if( strncmp(args[0], "get", 3) == 0 ){
			//get a file from the server
			int dc = createDataConnection(fd, hostName);
			if( dc == -1 ){
				//error creating data connection
				return 1;
			}

			//get the file
			char sc[PATH_MAX+2];
			snprintf(sc, strlen(args[1])+2, "G%s\n", args[1]); 
			write(fd, sc, strlen(sc));
			int err = waitForResponse(fd, ack, 256);
			if( err == -1 ){
				write(fd, "Q\n", 1);
				waitForResponse(fd, ack, 256);
				return 1;
			}
			if( strncmp(ack, "E", 1) == 0 ){
				printf("%s\n", ack);
				continue;
			}
			
			char fname[256];
			fileNameFromPath(sc, fname);
			//open a file for writing, create if it doesn't exist,
			//  error if it already exists
			int fp = open(fname, O_WRONLY | O_CREAT | O_EXCL, 0774);
			if( fp == -1 ){
				fprintf(stderr, "Error: open reports %s\n", strerror(errno));
				continue;
			}

			char buffer[256];
			while( 1 ){
				int rd = read(dc, buffer, 256);
				if( rd < 0 ){
					fprintf(stderr, "Error: read reports %s\n", strerror(errno) );
					//exit the server process
					write(fd, "Q\n", 2);
					waitForResponse(fd, ack, 256);
					close(dc);
					return 1;
				}else if( rd > 0 ){
					//write data to file
					int fw = write(fp, buffer, rd);
					if( fw == -1 ){
						fprintf(stderr, "Error: write reports %s\n", strerror(errno));
						write(fd, "Q\n", 2);
						waitForResponse(fd, ack, 256);
						close(dc);
						return 1;
					}
				}else{
					break;
				}
			}
			close(fp);
			close(dc);
		}else if( strncmp(args[0], "put", 3) == 0 ){
			//send a file to the server
			int dc = createDataConnection(fd, hostName);
			if( dc == -1 ){
				//error creating data connection
				return 1;
			}
			char sc[PATH_MAX+2];
			snprintf(sc, strlen(args[1])+2, "P%s\n", args[1]);
			write(fd, sc, strlen(sc));
			int err = waitForResponse(fd, ack, 256);
			if( err == -1 ){
				continue;
			}
			if(strncmp(ack, "E", 1) == 0){
				printf("%s\n", ack);
				continue;
			}

			char filePath[PATH_MAX];
			snprintf(filePath, strlen(args[1]), "%s", args[1]);

			//open the file for reading
			int fp = open(filePath, O_RDONLY);
			if( fp == -1 ){
				fprintf(stderr, "Error: open reports %s\n", strerror(errno) );
				continue;
			}

			char buffer[256];
			while( 1 ){
				int rd = read(fp, buffer, 256);
				if( rd < 0 ){
					fprintf(stderr, "Error: read reports %s\n", strerror(errno) );
					break;
				}else if( rd > 0 ){
					int fw = write(dc, buffer, rd);
					if( fw == -1 ){
						fprintf(stderr, "Error: write reports %s\n", strerror(errno));
						break;
					}
				}else{
					break;
				}
			}
			close(fp);
			close(dc);
		}else if( strncmp(args[0], "help", 4) == 0 ){
			printf("Command\t\tDescription\n");
			printf("exit\t\tClose server connections and exit.\n");
			printf("cd <path>\tChange client directory.\n");
			printf("rcd <path>\tChange server directory.\n");
			printf("ls\t\tView files in client directory.\n");
			printf("rls\t\tView files in server directory.\n");
			printf("get <path>\tGet the file at <path> from the server.\n");
			printf("show <path>\tShow the contents of <path>.\n");
			printf("put <path>\tPut the file at <path> on the server.\n");
		}else{
			printf("Invalid command. For a list of valid commands, enter 'help'.\n");
		}
	}
	return 0;
}

int main( int argc, char* argv[] ){
	char* hostName;
	if( argc == 1 ){
		//default to localhost
		hostName = "localhost";
	}else if( argc == 2 ){
		hostName = argv[1];
	}else{
		printf("Usage: mftp <hostname>\n");
		exit(0);
	}

	int lfd = socket( AF_INET, SOCK_STREAM, 0 );
	if( lfd < 0 ){
		fprintf(stderr, "Error: socket reports %s\n", strerror(errno) );
		exit(1);
	}

	struct sockaddr_in servAddr;
	struct hostent* hostEntry;
	struct in_addr **pptr;
	memset( &servAddr, 0, sizeof(servAddr) );
	servAddr.sin_family = AF_INET;
	servAddr.sin_port = htons(PORT);

	hostEntry = gethostbyname(hostName);
	if( !hostEntry ){
		herror("Error: gethostbyname reports\n");
		exit(1);
	}

	pptr = (struct in_addr**) hostEntry->h_addr_list;
	memcpy(&servAddr.sin_addr, *pptr, sizeof(struct in_addr) );

	//connect to the server
	connect(lfd, (struct sockaddr*) &servAddr, sizeof(servAddr));
	
	printf("Client: connected to server with hostname %s\n", hostName);

	int cl = cmdLoop( lfd, hostName );
	if( cl != 0 ){
		exit(1);
	}
	return 0;
}
