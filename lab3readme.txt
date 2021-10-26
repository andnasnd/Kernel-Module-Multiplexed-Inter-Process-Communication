1. Team Members 
	Anand Nambakam 
	anambakam@wustl.edu

1a. Credits: 
	I used the GeeksForGeeks AVL tree implementation approved on Piazza. The code is at 
	https://www.geeksforgeeks.org/avl-tree-set-1-insertion/. I implemented an inorder traversal function,
	and header file for this program. The Makefile I included will link and compile the AVL 
	tree in the Client and Server programs. 

2. Server Design 
	Handling Command Line Arguments 
		On invocation, the Server program checks the number of command line arguments provided. Program 
		usage is described as "./lab3server <filename> <portnum>" indicating that a valid config 
		file is present in the local file directory, as well as a valid port for Client communication. 
		As described in instructions, the initial config file requires 1st line as Output File name (to 
		be created by the program) and the jumbled input files which are to be sent to and sorted by 
		connected Clients. 

		A checkfile function was created to confirm validity of variable amounts of input files provided 
		to the Server program. The checkfile function creates a file pointer, invokes fopen, and ensures 
		that the opened file is valid. If not, the program exits. 

		The command line argument port number is added to the socket structure. If a valid port is not 
		provided, the later binding of the socket fails, and the error is caught and output by the program.
		In the situation that command line arguments are invalid, the program exits with respective
		error code and usage message. 

	Handling Arbitrary Number of Fragment Files 
		The following "filecontext" struct was creates to store arbitrary numbers of fragment files: 
		malloc() was used to dynamically allocate filecontext structures in memory equal to 
		number of input files. Once dynamically allocated, filecontexts are instantiated 

		typedef struct {
			FILE * fp; //input file pointer (retrieved with checkfile() function) 
			int cfd; //connection file descriptor, retrieved by connect() to client socket
			int nBytes; //number of bytes in file, used to identify end of file in named pipe
			int bytesSortedContent; //track number of bytes sorted by Client 
			int assigned; //flag to identify if file is assigned to a Client 
			int sorted; //flag to identify if file has been sorted by Client 
			int sent; //flag to identify if file has been sent to Client 
			char *line; //current line sent to Client 
			size_t capacity; 
			size_t length; 
		} filecontext; 

		The epoll instance is used to monitor input files and incoming new Client connections. I set 
		maximum of 64 simultaneous epoll events, which are captured and connected to by Client programs. 
		Upon valid Client connection, a respective input file descriptor and socket are devoted to 
		requesting Client, and monitored with EPOLLIN event. epoll was chosen to scale well with large 
		number of files to be watched and incoming socket connections. 


	Socket Connection 
		If the Server has validated input files, it prints it's IP address and port number, to which 
		Client connections will connect. I tried to use gethostname() and gethostbyname() to print
		the IP address, but it was only returning loopback address. Instead, I made getIPAddress() function,
		which uses ifaddrs struct to retrieve IP address. 

		The socket is opened with command line portnumber, and SO_REUSEADDR is set, so that multiple 
		Clients can bind to the same Server port. After successful bind(), listen() is initiated
		for incoming connection sockets, and EPOLLIN event is watched. 

		As mentioned previously, epoll is used to monitor incoming socket connections. epoll_wait() 
		system call is used to wait for incoming connection sockets, up to 64 maximum connections. A 
		timeout of 5 milliseconds is used to prevent the Server from blocking for too long, allowing 
		Server to timely handle multiple Clients. 

		If a Client connection is established, an unassigned (filecontext[i].assigned = 0), unsorted 
		(filecontext[i].sorted = 0) input file is assigned to the Client. Lines are extracted from 
		assigned input file, newline is ensured at end of lines, and each line is sent to respective Client. 
		When End of File is reached, "-1" is sent, which is the indicator line number that the file is 
		completely sent to the Client. The respective input file descriptor is closed, and the Server 
		waits to receive data from Client via EPOLLIN. 

		If number of assigned files matches number of input files, the Server stops waiting for 
		connection requests by closing communication file descriptor cfd. Additionally, when Server
		receives all input lines from a Client, the client socket is closed and removed from epoll 
		queue. 

		When the Server has read all data from all clients and inserted all lines into AVL tree 
		(numSorted == nFiles), a flag is set, ending the event loop. After the event loop, 
		dynamically allocated memory is freed for files, (re)allocated line buffers for short reads/writes, 
		the AVL tree, and ipaddress. When the Server-side AVL tree is populated with all lines 
		from all Clients, every line is written to output file using inorder traversal. This ensures that
		the accumulated lines are sorted. After all lines are written to output file, the output file is 
		closed. 


	Data Structure for Receiving Input File Lines
		An AVL tree was used to store sorted input lines to be received from Client 
		programs. Upon epoll discovery of POLLIN event from communication socket, the Server program
		reads each received line and inserts them into the AVL tree, using extracted line number as key. 
		Insertion at the root costs O(logn), at both Server and Client side.
		If input files are validated, lines from respective files are read with fscanf(), 
		and newline '\0' is removed from the end of the file. Valid lines are stored with respective
		file pointers in the filecontexts struct. Number of lines in input files are tabulated in 
		filecontexts struct for each respective file. This is used to check for EOF. If EOF is reached,
		connection to Client socket is closed. 


	Handling "Short Reads" and "Short Writes" 
		If only a part of a line is read from Client, the server accumulates the characters in a buffer
		associated with the client connection. The server checks for new line character from Client, and 
		forms a line from the associated buffer, which is then processed. Processing involves retrieving 
		line number from line, and inserting (line number, line) in AVL tree. If there are no bytes 
		available in the buffer, the buffer is reallocated to hold the processed line. Every new line is 
		allocated respective buffer, in case any line is "short read/short write". 

	Note** The handle_error macro depends on errno being set. If it is called with positive errno, handle_error
	       immediately exits the program, and does not free dynamically allocated memory. 

		Another weakness is that if Client program does not communicate the sorted lines back to the 
		Server, then the Server cannot exit. The timeout on epoll_wait at least prevents a single 
		bad Client from blocking entire Server function, but the Server will not exit if any Client 
		does not return. If the number of bad Clients = 64 (max events allowed), then the Server 
		will not exit. 

3. Client Design 
	Handling Command Line Arguments
		The Client program takes IP address and port number as command line arguments. These are the 
		IP and port with which to attempt connection to Server. Both IP and port are retrieved 
		from command line arguments, and port number is initially checked to be valid (>0, although 
		does not check for reserved port numbers at this stage). After the socket is created, 
		the IP and port are assigned to the socket. If either of these values are invalid, the program
		indicates respective error message and exits. The usage of the program is defined as "./client 
		<ip address> <port number>". 

	Socket Connection
		If a socket connection is established by the Client to the Server via connect(), epoll_wait 
		is used to efficiently monitor EPOLLIN events from Server. If EPOLLIN is triggered, 
		the client repeatedly reads and stores data from named pipe. Specifically, the named pipe 
		is opened, and every line sent by Server is inserted into AVL tree. If line number "-1" is 
		received from Server, this indicates that EOF is reached by Client, and the lines inserted to the 
		AVL tree are written to the Server by inorder traversal, ensuring they are sorted. If EPOLLRDHUP 
		is sent by Server, the event loop ends and Client connection is closed. The server 
		initiates EPOLLRDHUP when it has finished receiving all lines from a respective client. 

4. Build Instructions 
	1. Ensure all files (lab3server.c, lab3client.c, avl.h, avl.c, Makefile) are within the same directory. 

	2. Within directory, issue "make" to invoke the Makefile, which links the AVL tree programs to the 
		Server and Client respectively and produces executables for testing. 

	3. Usage for Server: ./lab3server <config file> <port number> 
		3a. For config file, I used "testfile", which I will include with my code. An alternative 
			testfile can be created with arbitrary (<64) number of fragment files, where first 
			line is output file to be created by Server. 

	4. Usage for Client: ./lab3client <IP address> <port number> 

	*Note: MacOS does not support epoll. When running this program on multiple machines, they have to be 
		Linux-based. 

5. Testing and Evaluation
	
	For testing, I used the fileshufflecut.cpp program, and generated fragment files. I then created 
	the config file with first line output file, and subsequent lines as input files. 

	1. Run server and client on same machine 
		Works properly, with arbitrary (<MAX_EPOLL_EVENTS=64) fragment files. 
	2. Run server and client on same machine, erroneous lines (missing line number)
		BUG: The Server blocks. Unfortunately I did not have time to handle this important error 
		case. Programs work if a line number is provided with blank content, but if no line number 
		is provided, the program does not skip the erroneous line. 
	3. Run server and client on different machines 
		Works properly 

	Error Handling 
	1. Test Invalid IP, Port 
		Error Handled 
	
	2. Test Invalid config file 
		Error Handled
	
	3. Test Invalid input files 
		Error Handled 

6. Development Effort 
	~30 hours 






	 

		