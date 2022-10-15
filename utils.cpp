#include <iostream>
#include <cstring>
#include <cstdlib>
#include <vector>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include "include/defines.h"

bool is_number(const char *str, const int len) {
    // Go through the entire string and check if all characters are digits
    for (int i = 0; i < len; ++i) {
        if (str[i] < '0' || str[i] > '9') {
            return false;
        }
    }

    return true;
}

int recv_messages(const int tcp_socket, std::vector<char *> &messages) {
    // Declare a buffer to receive the data
    char buffer[BUFLEN];
    memset(buffer, 0, BUFLEN);

    // Receive the first message
    int n = recv(tcp_socket, buffer, BUFLEN, 0);
    if (n < 0) {
        fprintf(stderr, "Error reading from TCP socket.\n");
        return -1;
    }

    if (n == 0) {
        // The connection to the server was lost
        return 0;
    }
    
    // Attempt to get more messages
    int curr_len = 0;
    while (true) {
        // Check if we've reached the end of the buffer
        if (curr_len == BUFLEN) {
            break;
        }

        // Read the length of the message
        int msg_len = 0;
        memcpy(&msg_len, buffer + curr_len, 2);
        msg_len = ntohs(msg_len);

        // If the length is 0, we've finished receiving the messages
        if (msg_len == 0) {
            break;
        }

        // Calculate the remaining length
        int remaining_length = BUFLEN - curr_len;

        // Create a new message
        char *new_msg = new char[msg_len];
        if (!new_msg) {
            fprintf(stderr, "Couldn't allocate memory for new_msg.\n");
            return -1;
        }

        // Check if we can fit in an entire message
        if (msg_len <= remaining_length) {
            // If we can, copy the entire message from the buffer
            memcpy(new_msg, buffer + curr_len, msg_len);
            curr_len += msg_len;
        } else {
            // Otherwise, copy the first part of the message
            memcpy(new_msg, buffer + curr_len, remaining_length);

            // Receive more bytes
            memset(buffer, 0, BUFLEN);
            int n = recv(tcp_socket, buffer, BUFLEN, 0);
            if (n < 0) {
                fprintf(stderr, "Error reading from TCP socket.\n");
                return -1;
            }

            if (n == 0) {
                return 1;
            }

            // Copy the last part of the message
            memcpy(((char *)new_msg) + remaining_length, buffer, msg_len - remaining_length);
            curr_len = msg_len - remaining_length;
        }

        // Add the message to the list
        messages.push_back(new_msg);
    }

    return 1;
}
