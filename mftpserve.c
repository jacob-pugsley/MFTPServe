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
#include <time.h>
#include <sys/wait.h>
#include <limits.h>
#include <sys/stat.h>
#include "mftp.h"

#define PORT 49999

void returnFailure(char* fncName, int errCode, int fd){
	//write an error message to stdout and to the client
	printf("FAILED: %s reports %s.\n", fncName, strerror(errCode) );
	char err[256] = "E";
	strncat(err, strerror(errCode), 253);
	strcat(err, "\n");
	write(fd, err, strlen(err));
} 

void returnGenericFailure(char* message, int fd){
	//write an error message to stdout and to the client
	printf("FAILED: %s\n", message );
	char err[256] = "E";
	snprintf(err, strlen(message)+2, "E%s\n", message);
	write(fd, err, strlen(err));
}

void returnAck(int fd){
	//send acknowledgement to the client
	write(fd, "A\n", 2);
}

int waitForResponse( int fd, char* buffer, int maxChars ){
	//read server responses until a newline is encountered
	//  or the buffer size is exceeded	
	memset(buffer, 0, maxChars);
	int rd = -1;
	char ch;
	int bp = 0;
	while((rd = read(fd, &ch, 1)) > 0 && bp < maxChars){
		if( ch == 10 ){
			break;
		}
		buffer[bp] = ch;
		bp++;
	}
	if( read < 0 ){
		returnFailure("read", errno, fd);
		return -1;
	}
	return bp;
}

int createDataConnection( int fd ){
	//make a new socket for the data connection
	int dfd = socket( AF_INET, SOCK_STREAM, 0 );
	
	struct sockaddr_in servAddr;
	memset(&servAddr, 0, sizeof(servAddr));
	servAddr.sin_family = AF_INET;
	servAddr.sin_port = htons(0);
	servAddr.sin_addr.s_addr = htons(INADDR_ANY);

	if( bind( dfd, (struct sockaddr*) &servAddr, sizeof(servAddr)) < 0 ){
		perror("bind");
		return -1;
	}
	
	//get the port number
	struct sockaddr_in sockAdd;
	memset(&sockAdd, 0, sizeof(sockAdd));
	unsigned int lengthOfSockAdd = sizeof(sockAdd);
	if( getsockname( dfd, (struct sockaddr*) &sockAdd, &lengthOfSockAdd) < 0 ){
		perror("getsockname");
		return -1;
	}
	
	//listen to the socket
	int err = listen( dfd, 1 ); //only one child per data connection
	if( err != 0 ){
		returnFailure("listen", errno, fd);
		return -1;
	}
	//send acknowledgement to client
	char ack[8]; 
	snprintf(ack, 7, "A%d", ntohs(sockAdd.sin_port));
	strcat(ack, "\n");
	write(fd, ack, strlen(ack));

	//wait for the client to send something
	socklen_t acceptfd;
	unsigned int len = sizeof(struct sockaddr_in);
	struct sockaddr_in clientAddr;
	acceptfd = accept( dfd, (struct sockaddr*) &clientAddr, &len );
	if( acceptfd == -1 ){
		returnFailure("accept", errno, fd);
		return -1;
	}
	struct hostent* hostEntry;
	hostEntry = gethostbyaddr( &(clientAddr.sin_addr), sizeof(struct in_addr), AF_INET );
	if( !hostEntry ){
		herror("gethostbyaddr");
		return -1;
	}
	//return the file descriptor once it has a connection
	return acceptfd;
}

void fileNameFromPath( char* path, char* fname ){
	//get the last part of a file path
	//  and store it in fname
	int i = strlen(path)-1;
	while( i > 0 && path[i] != 47){
		i--;
	}
	int j = 0;
	while( i < strlen(path) ){
		fname[j] = path[i];
		j++;
		i++;
	}
	strcat(fname, "\0");
}

int ls( int dfd, int fd ){
	//fork and exec ls -l, sending output to data connection
	int pid = fork();
	if(pid == 0){
		//child
		close(1);
		dup(dfd);
		close(dfd);
		execl("/bin/ls", "ls", "-l", (char*)0);
		returnFailure("exec", errno, fd);
		return -1;
	}else if( pid < 0 ){
		returnFailure("fork", errno, fd);
		return -1;
	}else{
		//parent
		wait(NULL);
	}
	return 0;
}

int cmdLoop( int fd, char* hostName ){
	char cmd[256];
	int dfd = -1;
	
	while( 1 ){
		//take commands from the client and execute them
		memset(cmd, 0, 256);
		int rd = -1;
		char ch;
		int bp = 0;
		while( (rd = read(fd, &ch, 1)) > 0 && bp < 256 ){
			if( ch == 10 ){
				break;
			}
			cmd[bp] = ch;
			bp++;
		}
		cmd[bp] = '\0';
		printf("Alert: received %s\n", cmd);

		//got a command
		if( strncmp(cmd, "Q", 1) == 0 ){
			returnAck(fd);
			close(fd);
			if( dfd != -1 ){
				close(dfd);
			}
			break;
		}else if( strncmp(cmd, "D", 1) == 0 ){
			dfd = createDataConnection(fd);
			if( dfd == -1 ){
				returnGenericFailure("Unable to create data connection", fd);
				return 1;
			}
		}else if( strncmp(cmd, "G", 1) == 0 ){
			//send file to client over data connection
			if( dfd == -1 ){
				returnGenericFailure("No data connection", fd);
				continue;
			}			
			 
			char filePath[PATH_MAX];
			for( int i = 1; i <= bp; i++ ){
				if( i == bp ){
					filePath[i-1] = '\0';
				}else{
					filePath[i-1] = cmd[i];
				}
				
			}
			//attempt to open the file for reading
			int fp = open(filePath, O_RDONLY);
			if( fp == -1 ){
				returnFailure("open", errno, fd);
				close(dfd);
				dfd = -1;
				continue;
			}
			struct stat sb;
			int err = fstat(fp, &sb);
			if( err != 0 ){
				returnFailure("fstat", errno, fd);
				close(fp);
				close(dfd);
				dfd = -1;
				continue;
			}
			if( !S_ISREG(sb.st_mode) ){
				returnGenericFailure("Not a regular file", fd);
				close(fp);
				close(dfd);
				
				dfd = -1;
				continue;
			}
			//send data to the client
			returnAck(fd);
			char buffer[256];
			while( 1 ){
				int rd = read(fp, buffer, 256);
				if( rd < 0 ){
					returnFailure("read", errno, fd);
					close(dfd);
					dfd = -1;
					continue;
				}else if( rd > 0 ){
					int fw = write(dfd, buffer, rd);
					if( fw == -1 ){
						returnFailure("write", errno, fd);
						close(dfd);
						dfd = -1;
						continue;
					}
				}else{
					break;
				}
			}
			
			close(dfd);
			dfd = -1;
		}else if( strncmp(cmd, "P", 1) == 0 ){
			//receive a file from the client over data connection
			if( dfd == -1 ){
				returnGenericFailure("No data connection", fd);
				continue;
			}
			char p[PATH_MAX];
			for( int i = 1; i < strlen(cmd); i++ ){
				p[i-1] = cmd[i];
			}

			char fname[256];
			fileNameFromPath(p, fname);

			//open a file for writing and create if it doesn't exist
			//  error if it already exists
			int fp = open(p, O_WRONLY | O_CREAT | O_EXCL | 0744);
			if( fp == -1 ){
				returnFailure("open", errno, fd);
				continue;
			}
			struct stat sb;
			int err = fstat(fp, &sb);
			if( err != 0 ){
				returnFailure("fstat", errno, fd);
				close(fp);
				close(dfd);
				dfd = -1;
				continue;
			}
			if( !S_ISREG(sb.st_mode) ){
				returnGenericFailure("Not a regular file", fd);
				close(fp);
				close(dfd);
				dfd = -1;
				continue;
			}
			returnAck(fd);
			int failed = 0;
			char buffer[256];
			while( 1 ){
				int rd = read(dfd, buffer, 256);
				if( rd < 0 ){
					returnFailure("read", errno, fd);
					failed = 1;
					break;
				}else if( rd > 0 ){
					int fw = write(fp, buffer, rd);
					if( fw == -1 ){
						returnFailure("write", errno, fd);
						failed = 1;
						break;
					}
				}else{
					break;
				}
			}
			close(fp);
			close(dfd);
			dfd = -1;
			if( failed == 0 ){
				printf("Success: wrote file.\n");
			}
		}else if( strncmp(cmd, "C", 1) == 0 ){
			//change directory
			char dir[PATH_MAX] = "";
			for( int i = 1; cmd[i] != 0 && cmd[i] != 10; i++ ){
				dir[i-1] = cmd[i];
			}
			if( chdir(dir) != 0 ){
				returnFailure("chdir", errno, fd);
			}else{
				printf("Success: changed directory to %s\n", dir);
				returnAck(fd);
			}
		}else if( strncmp(cmd, "L", 1) == 0 ){
			//list directory
			if( dfd == -1 ){
				returnGenericFailure("No data connection", fd);
				continue;
			}
			int l = ls(dfd, fd);
			if( l != -1 ){
				printf("Success: listed directory\n");
				returnAck(fd);
			}
			close(dfd);
			dfd = -1;
		}else{
			returnGenericFailure("Invalid command", fd);
		}
	}
	return 0;
}

int main(){
	printf("Alert: Connecting...\n");
	int lfd = socket(AF_INET, SOCK_STREAM, 0);
	if( lfd < 0 ){
		fprintf(stderr, "Error: socket reports %s\n", strerror(errno) );
		exit(1);
	}

	struct sockaddr_in servAddr;

	memset(&servAddr, 0, sizeof(servAddr));
	servAddr.sin_family = AF_INET;
	servAddr.sin_port = htons(PORT);
	servAddr.sin_addr.s_addr = htonl(INADDR_ANY);

	if( bind( lfd, (struct sockaddr*) &servAddr, sizeof(servAddr)) < 0 ){
		perror("bind");
		exit(1);
	}

	int list = listen( lfd, 4 ); //up to four child processes
	if( list < 0 ){
		fprintf(stderr, "Error: listen reports %s\n", strerror(errno) );
		exit(1);
	}

	socklen_t cfd;
	unsigned int len = sizeof(struct sockaddr_in);
	struct sockaddr_in clientAddr;

	while( 1 ){
		//run forever
		cfd = accept( lfd, (struct sockaddr*) &clientAddr, &len );
		if( cfd < 0 ){
			fprintf(stderr, "Error: accept reports %s\n", strerror(cfd) );
			exit(1);
		}

		//fork
		int pid = fork();
		if( pid == 0 ){
			//child
			struct hostent* hostEntry;
			char* hostName;

			hostEntry = gethostbyaddr( &(clientAddr.sin_addr), sizeof(struct in_addr), AF_INET );
			if( !hostEntry ){
				herror("gethostbyaddr");
				  
				exit(1);
			}

			hostName = hostEntry->h_name;
			printf("Success: Recieved connection from %s\n", hostName);
			int cl = cmdLoop( cfd, hostName );
			if( cl != 0 ){
				exit(1);
			}
			exit(0);
		}else if( pid < 0 ){
			fprintf(stderr, "Error: fork reports %s\n", strerror(errno) );
			exit(1);
		}else{
			//parent
			if( close(cfd) < 0 ){
				fprintf(stderr, "Error: close reports %s\n", strerror(errno) );
				exit(1);
			}

			//wait for all children to exit
			while( 1 ){
				if( waitpid(-1, 0, WNOHANG) <= 0 ){
					break;
				}
			}	
		}
	}
	return 0;
}
