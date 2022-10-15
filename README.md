Mihnea Tudor \
Group 321CAb

# Client - Server application 
## Description
This application has a total of 3 main entities: the UDP clients, the TCP
clients and the server. The TCP clients can subscribe to and unsubscribe from
different topics of choice by sending messages to the server. The UDP clients
send messages, each particular to a topic, to the server, that are then
distributed to the clients. If the client isn't connected to the server when
a UDP message is received, it can either be thrown away or saved in that
client's queue of messages, depending on the client's preference.

Each entity will be explained in the following sections, with implementation
details located towards the end.


## The UDP Client
Already implemented by the Network Protocols team.

A client that sends messages to the server through the UDP protocol. Each
message has a fixed format:
 * first 50 bytes - the topic
 * next byte - the data type
 * next bytes until the end (at most 1500) - the content

Depending on the data type, the content has 4 possible formats:
 * type 0 - INT
 * type 1 - SHORT_REAL
 * type 2 - FLOAT
 * type 3 - STRING


## The TCP Client
A TCP client is run using the command:

```./subscriber <ID_CLIENT> <SERVER_IP> <SERVER_PORT>```

When run, a TCP socket is opened, after which a connection with the server
(at the given IP:Port) is attempted (and, if successful, established). Nagle's
algorithm is also disabled on the socket.

The first step after establishing the connection is to let the server know the
client's ID, given as a parameter. This is done by sending a message to the
server, containing only a string with no whitespace, with at most 10 characters
(each message between the client and server uses a framing protocol described
later).

After all of this is done, the client needs to be able to read both from
standard input AND from the TCP socket (messages received from the server),
in parallel. This is done by using multiplexing, process described in the
'Implementation Details' section.

### Receiving from stdin
There are 3 cases:
 * "exit" is received, in which case we close the TCP socket and the client
   altogether
 * "subscribe" is received, followed by a topic (at most 50 characters) and a
   flag for the store & forward option (described later)
 * "unsubscribed" is received, followed by a topic

In the latter 2 cases, we send a message to the server to inform it of our
intention. Before being sent, the message is framed using a protocol described
later.

### Receiving on the TCP socket
Receiving a message here means receiving a message from the server, therefore
receiving a message that originated from a UDP client, with a topic of interest
to the TCP client. The only step that remains is parsing the content of the
message, depending on its data type, and printing the result to stdout.


## The Server
The server is run using the command:

```./server <SERVER_PORT>```

When run, both a TCP socket and a UDP socket are opened, and are both bound to
the server port given as a parameter. The TCP socket is also set to listen to
new connections from TCP clients. Nagle's algorithm is disabled on the TCP
socket.

Using multiplexing (described later), we can listen in parallel to stdin, TCP
and UDP sockets (and later, clients).

### Receiving from stdin
The only command that can be received from stdin is "exit", which closes all
sockets, frees the dynamically allocated memory, and closes the server.

### Receiving on the UDP socket
The server receives messages from UDP clients, extracts the topic and the
data type, and sends a shortened message (based on the data type) towards the
TCP clients. However, before sending the message, the SF flag (explained at the
end of the document) is checked, and the message is either thrown away or
stored in memory.

### Receiving on the TCP socket
On the TCP socket, the server can only check for new connection requests coming
from TCP clients. When such a connection is received, the client is partially
initialized (we need to receive its ID, however it is received on its own fd)
and its IP and port are stored in a structure (they are needed to print a
message to the console, later, when handling the client).

### Receiving from a client
Each client has a designated file descriptor, where messages are received. When
a message is received from a client, we check if it has not been fully
initialized yet (if we still require its ID), in which case, we've two
options:
 * the client with the received ID is already connected, thus we close the
   new connection
 * the client with the received ID is NOT already connected, in which case we
   finish initializing it. This means that we also send all messages in the
   client's message queue towards him

However, if it HAS already been fully initialized, that means we've received
a command from the client (either subscribe or unsubscribe), and we need to
update the data structures accordingly. A summary of the data structures is
located in the next section.


## Implementation Details
### Multiplexing
Both the client and the server, to be able to read input from multiple file
descriptors at once, need to implement a multiplexing protocol using file
descriptor sets. For this, we need to create a set of descriptors and add to it
all the files we want to read from: stdin, TCP socket, and, in the case of the
server, UDP socket and a socket for each connected client.

This permanent reading of input, in parallel, is done in an infinite while loop
using a function named select(). The function blocks the execution of the
program and listens to changes on any of the active file descriptors (that were
added to the set beforehand).

When a fd is updated, select() resumes the program, and we check which
file we can read data from (using a for loop through all possible file
descriptors).

### Message structures
Each type of message (TCP client -> server, UDP client -> server, server -> TCP
client) has a certain structure designed specifically for it. The fields are
arranged in such a way that reading from and writing to such a structure is
trivial (and in the order described in the UDP message format), and extracting
information from the message's content is simple with the use of unions.

All structures and unions are marked with ```__attribute__((packed))```, in
order to align the data to a single byte, therefore to easily read and write
data directly into the structure.

### Storage / Data structures
There are a few important data structures used by the server, those being:
 * (map) id_to_client - a map from a string (the client's ID) to the client
   itself; acts as a database, storing all clients ever connected to the server
 * (map) fd_to_client - "dynamic" map, holding all currently connected clients;
   it's a map from a client's file descriptor to the client itself, and is
   updated every time a client connects / disconnects
 * (map) uninitialized_fds - also a "dynamic" map, updating every time a new
   client connection request is received on the TCP socket, but still requires
   to be associated with an ID; the entry is removed when the client is fully
   initialized (the server receives its ID)
 * (map) name_to_topic - a map holding all subscriptions that are currently
   active (when the server receives a UDP message, this is where it searches
   for the destination clients); a subscription consists of a pointer to the
   subbed client and the associated SF flag

### Store & Forward
This concept refers to storing messages that need to reach a certain client,
when that client is not currently connected to the server, but the client
doesn't want to miss them. The implementation is simple, each client has a
queue of messages assigned to it that gets filled when a UDP message is
received and the client is offline. When the client reconnects, the queue
is emptied message by message, sending them towards the destination.

To save memory (to only allocate the memory for the message once), a shared
pointer is used, keeping track of all the queues is still exists in, and when
the last instance of the message is sent to the client, the memory is freed.

### Message framing
In the process of communication between the TCP clients and the server, 
because of message concatenation and truncation, the need arises to create a
protocol to somehow delimit messages from one another.

In this application, this is handled by always sending the length of the
message to be sent, at the beginning of the actual message. Because the
maximum buffer size is low, we can represent this length using two bytes,
thus adding a short "header" to each sent message. THe length is represented
in network order, as we are transmitting it through the network.
