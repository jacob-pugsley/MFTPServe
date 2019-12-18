#ifndef mftp
#define mftp

int cmdLoop( int fd, char* hostName ); //loop for sending/receiving commands
					   
void fileNameFromPath(char* path, char* fname); //gets the last part of a file path

#endif
