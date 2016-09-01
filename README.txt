Since we graduates only have access to csgrad* server.
The project has only been tested on csgrads1 server.
Not been tested on cs* servers.

Compile command line: 
gcc server.c -o server -pthread
gcc client.c -o client -pthread

Run command line examples:
server:
	./server 9999
client:		
	./client csgrads1 9999

Zengtai Qi
zxq150130