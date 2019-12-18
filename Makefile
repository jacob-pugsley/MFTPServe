OBJS = mftp.o mftpserve.o
CCFLAGS = -Wall -D_DEFAULT_SOURCE
CC = gcc

all: mftp mftpserve

mftp: mftp.c mftp.h
	${CC} ${CCFLAGS} mftp.c -o mftp

mftpserve: mftpserve.c mftp.h
	${CC} ${CCFLAGS} mftpserve.c -o mftpserve

clean:
	rm mftp mftpserve