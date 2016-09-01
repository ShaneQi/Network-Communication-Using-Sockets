#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <netdb.h>
#include <pthread.h>
#include <semaphore.h>

#define BUFSIZE 81 

void* handleClient(void *);

/* Semaphores for mutex. */
sem_t users_mutex;
sem_t online_mutex;
sem_t message_mutex;

int main(int argc, char * argv[]) {

	char 		host[100];
	int 		port;
	int			sd, sd_current;
	int 		*sd_client;
	int 		addrlen;
	struct 		sockaddr_in sin;
	struct 		sockaddr_in pin;
	pthread_t 	tid;
	pthread_attr_t attr;

	/* Init semaphores. */
	sem_init(&users_mutex, 0, 1);
	sem_init(&online_mutex, 0, 1);
	sem_init(&message_mutex, 0, 1);

	/* Check for command line arguements. */
	if (argc != 2) {
		printf("Please enter server port.\n");
		exit(1);
	}

	/* Get port from argv. */
	port = atoi(argv[1]);

	/* Create an internet domain stream socket. */
	if ((sd = socket(AF_INET, SOCK_STREAM, 0)) == -1) {
		perror("Error creating socket");
		exit(1);
	}

	/* Complete the socket structure. */
	memset(&sin, 0, sizeof(sin));
	sin.sin_family = AF_INET;
	sin.sin_addr.s_addr = INADDR_ANY;		/* Any address on this host. */
	sin.sin_port = htons(port);					/* Convert to network byte order. */

	/* bind the socket to the address and port number */
	if (bind(sd, (struct sockaddr *) &sin, sizeof(sin)) == -1) {
		perror("Error on bind call");
		exit(1);
	}

	/* set queuesize of pending connections */ 
	if (listen(sd, 5) == -1) {
		perror("Error on listen call");
		exit(1);
	}

	/* announce server is running */
	gethostname(host, 80); 
	printf("Server is running on %s:%d\n", host, port);

	/* wait for a client to connect */
	pthread_attr_init(&attr);
	pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED); /* use detached threads */
	addrlen = sizeof(pin);
	while (1)
	{
		if ((sd_current = accept(sd, (struct sockaddr *)  &pin, (socklen_t*)&addrlen)) == -1) 
		{
			perror("Error on accept call");
			exit(1);
		}

		sd_client = (int*)(malloc(sizeof(sd_current)));
		*sd_client = sd_current;

		pthread_create(&tid, &attr, handleClient, sd_client);
	}

	/* close socket */
	close(sd);
}


char users[100][BUFSIZE];
int usersPointer = 0;
int online[100] = {0};
char message[1000][BUFSIZE];
int reciever[1000];
char messages[100][10][BUFSIZE];
char messagesHeader[100][10][BUFSIZE];
char tab[2] = "\t";

/* 
 * If the string ends with '\n', replace it with '\t',
 * otherwise append a '\t' after the string.
 * '\t' indicates the end of the string. 
 */
void wrap(char str[]) {
	if ( str[strlen(str)-1] == '\n') {
		str[strlen(str)-1] = '\t';
	} else {
		strcat(str, "\t");
	}
}

/* 
 * Replace '\t' in the end with null character. 
 */
void unwrap(char str[]) {
	str[strlen(str)-1] = '\0';
}

/*
 * Keep reading until encounter '/t' which indicates the end of the message.
 */
void read_socket(int sd, char * targetStr) {
	char temp[20*BUFSIZE] = "0";
	strcpy(targetStr, "");
	while (temp[strlen(temp) - 1] != '\t') {
		if (read(sd, temp, BUFSIZE) == -1) {
			perror("ERROR on read call.");
			exit(1);
		}
		strcat(targetStr, temp);
	}
	unwrap(targetStr);
}

/*
 * Before write message, wrap it with '\t' in the ending.
 */
void write_socket(int sd, char * targetStr) {
	wrap(targetStr);
	int size = -1;
	if ( (size = write(sd, targetStr, strlen(targetStr))) == -1 ) {
		perror("Error on write call.");
		exit(1);
	}
	unwrap(targetStr);
}


/*
 * Insert user into users array if he/she is unknow.
 */
int putUser(char * name) {
	sem_wait(&users_mutex);
	int i = 0;
	/* If the user is a known user, return the userId. */
	for (i=0; i<usersPointer; i++){
		if (strcmp(name, users[i]) == 0) {
			sem_post(&users_mutex);
			return i;
		} 
	}
	/* If the user is an unknown user, return (100 + userId). */
	strcpy(users[usersPointer], name);
	usersPointer++;
	sem_post(&users_mutex);
	return (100 + usersPointer - 1);
}

/*
 * If the user is unknown, putUser functino will return the value (userId + 100),
 * so client has to unwrap it to actual userId.
 */
void unwrapUserId(int * userId) {
	if (*userId >= 100) {
		*userId -= 100;
	} 
}

/* 
 * Wrap the log description with time stamp and output. 
 */
void serverLog(char * logDes) {
	time_t now = time(0);
	char nowStr[30];
	strftime (nowStr, 30, "%m/%d/%y, %I:%M %p, ", localtime (&now));
	printf("%s%s\n", nowStr, logDes);
}

/* 
 * Save message to messages array, 
 * also save message header which contains time stamp and sender.
 */
void saveMessage(char * sender, int userId, char * message) {
	sem_wait(&message_mutex);
	int i = 0;
	while (i < 10) {
		if (messages[userId][i][0] == 0) {
			strcpy(messages[userId][i], message);
			char header[BUFSIZE];
			time_t now = time(0);
			char nowStr[30];
			strftime (nowStr, 30, "%m/%d/%y, %I:%M %p, ", localtime (&now));
			sprintf(header, "From %s, %s", sender, nowStr);
			strcpy(messagesHeader[userId][i], header);
			sem_post(&message_mutex);
			return;
		}
		i++;
	}
	sem_post(&message_mutex);
}


/*
 * When user login, set its online flag to 1.
 */
void setOnline(int userId, int isOnline) {
	sem_wait(&online_mutex);
	online[userId] = isOnline;
	sem_post(&online_mutex);
}

void* handleClient(void *arg) {

	char name[BUFSIZE];
	int userId;
	char selection[5] = "0";
	char temp[BUFSIZE];
	int sd = *((int*)arg);
	int i = 0;
	free(arg);
	read_socket(sd, name);

	/* Control + c handler. */
	if (name[0] == 27) {
		return;
	}

	/* Put user and get userId. */
	userId = putUser(name);
	if (userId >= 100) {
		char logDes[BUFSIZE];
		sprintf(logDes, "Connection by unknown user %s.", name);
		serverLog(logDes);
	} else {
		char logDes[BUFSIZE];
		sprintf(logDes, "Connection by known user %s.", name);
		serverLog(logDes);
	}
	sprintf(temp, "%d", userId);
	unwrapUserId(&userId);
	if (online[userId]) {
		strcpy(temp, "-1");
		write_socket(sd, temp);
		return;
	}

	/* Notify client the userId. */
	write_socket(sd, temp);

	/* Set online flag. */
	setOnline(userId, 1);

	/* Keep respond to client until get '7'. */
	while (selection[0] != '7') {
		read_socket(sd, selection);
		char logDes[BUFSIZE];
		/* Control + c handler. */
		if (selection[0] == 27) {
			sprintf(logDes, "%s exits.", name);
			serverLog(logDes);
			setOnline(userId, 0);
			return;
		}
		switch (selection[0]) {

			case '1':
			sprintf(logDes, "%s displays all known users.", name);
			serverLog(logDes);
			i = 0;
			strcpy(temp, "");
			/* Iterate the users array and send the user list. */
			while (i < usersPointer) {
				char item[BUFSIZE];
				sprintf(item, "%s\f", users[i]);
				strcat(temp, item);
				i++;
			}
			write_socket(sd, temp);
			break;

			case '2':
			sprintf(logDes, "%s displays all connected users.", name);
			serverLog(logDes);
			i = 0;
			strcpy(temp, "");
			/* Iterate the users array and check online flag,
			 * send the online user list. */
			while (i < usersPointer) {
				if (online[i]) {
					char item[BUFSIZE];
					sprintf(item, "%s\f", users[i]);
					strcat(temp, item);
				}
				i++;
			}
			write_socket(sd, temp);
			break;

			case '3':
			read_socket(sd, temp);
			/* Control + c handler. */
			if (temp[0] == 27) {
				sprintf(logDes, "%s exits.", name);
				serverLog(logDes);
				setOnline(userId, 0);
				return;
			}
			char split[2];
			split[0] = 10;
			char *token;
			token = strtok(temp, split);
			/* message[0] stores the recipient, 
			 * message[1] stores the message content. */
			char message[2][BUFSIZE];
			i = 0; 
			/* walk through other tokens */
			while( i<=1 ) 
			{
				strcpy(message[i], token);
				i++;
				token = strtok(NULL, split);
			}
			/* Get userId, if user is unknow, also make it known. */
			int recipientId = putUser(message[0]);
			unwrapUserId(&recipientId);
			/* Store message. */
			saveMessage(name, recipientId, message[1]);
			sprintf(logDes, "%s posts a message for %s.", name, message[0]);
			serverLog(logDes);
			break;

			case '4':
			read_socket(sd, temp);
			/* Control + c handler. */
			if (temp[0] == 27) {
				sprintf(logDes, "%s exits.", name);
				serverLog(logDes);
				setOnline(userId, 0);
				return;
			}
			i = 0;
			/* Save message to every users. */
			while (i < usersPointer) {
				if (online[i]) {
					saveMessage(name, i, temp);
				}
				i++;
			}
			sprintf(logDes, "%s posts a message for all currently connected users.", name);
			serverLog(logDes);
			break;

			case '5':
			read_socket(sd, temp);
			/* Control + c handler. */
			if (temp[0] == 27) {
				sprintf(logDes, "%s exits.", name);
				serverLog(logDes);
				setOnline(userId, 0);
				return;
			}
			i = 0;
			/* Save message to all online users. */
			while (i < usersPointer) {
				saveMessage(name, i, temp);
				i++;
			}
			sprintf(logDes, "%s posts a message for all know users.", name);
			serverLog(logDes);
			break;

			case '6':
			sprintf(logDes, "%s gets messages.", name);
			serverLog(logDes);
			i = 0;
			char messagesBuf[20*BUFSIZE];
			strcpy(messagesBuf, "");
			/* Send the user's all messages. */
			while (messages[userId][i][0] != 0 && i < 10) {
				char bullet[5];
				sprintf(bullet, "  %d. ", i+1);
				strcat(messagesBuf, bullet);
				strcat(messagesBuf, messagesHeader[userId][i]);
				strcat(messagesBuf, messages[userId][i]);
				strcat(messagesBuf, "\n");
				i++;
			}
			/* Notify user there is no messages. */
			if (messagesBuf[0] == 0) {
				strcat(messagesBuf, "  No Messages for you.\n");
			}
			write_socket(sd, messagesBuf);
			break;

			case '7':
			sprintf(logDes, "%s exits.", name);
			serverLog(logDes);
			setOnline(userId, 0);
			break;

			/* Wrong command case. */
			default:
			break;
		}
	}
	close(sd);
}
