#include <iostream>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/time.h>
#include <algorithm>

#define PACKET_TIMEOUT_WAIT 3
#define FAILURES_AMOUNT 7
#define ECHOMAX 516
#define DATAMAX 512
#define OPCODE_LEN 2
#define DATA_BLOCK_NUM_LEN 2
#define WRQ_OPCODE 2
#define ACK_OPCODE 4
#define DATA_OPCODE 3
#define FAILURE -1
#define SUCCESS 0
#define DATA_ALERT_TIMEOUT_LIMIT 4
#define DATA_ALERT_TIMEOUT 1
#define DATA_ALERT_OPCODE 2
#define DATA_ALERT_BLOCK_NUM 3
#define DATA_ALERT_SUCCESS 0
#define HEADER_TFTP 4


bool is_digits(char* str);

using namespace  std;
int main(int argc, char **argv) {
  //TODO: wrong opcode should print only once?
  //// checking there is just one input argument - the port number.
  int retval = SUCCESS;
  if (argc != 2) {
    cout << "FLOWERROR: Invalid Paramters" << endl; // TODO: maybe check iflonger than short int port is int?
    retval = FAILURE;
  } else {
    bool is_int = is_digits(argv[1]);
    if (!is_int) {
      retval = FAILURE;
      cout << "FLOWERROR: Invalid Paramters" << endl;
    }
  }
  if (retval == FAILURE){
    return retval;
  }
  int port = atoi(argv[1]);

  //// declaring the ACK-PACKET struct
  struct ACK_packets {
    uint16_t Opcode;
    uint16_t Block_Number_being_acknowledged;
  }__attribute__((packed));

  uint16_t  ACK_number=0;

  //// declaring variables for future use
  int timeout_expired_count = 0;
  char message_buffer[ECHOMAX] = {0};
  unsigned int client_address_len;
  int recieved_msg_size;
  int data_alert = DATA_ALERT_SUCCESS;
  struct sockaddr_in server_address, client_address;

  ////open socket for server
  int sock_fd = socket(AF_INET, SOCK_DGRAM, 0); // GOOD
  if (sock_fd < 0) {
    perror("TTFTP_ERROR: socket() fail: ");
    return  FAILURE;
  }

  //// initializing the server parameters
  memset(&server_address, 0, sizeof(server_address));
  server_address.sin_addr.s_addr = htonl(INADDR_ANY);
  server_address.sin_family = AF_INET;
  server_address.sin_port = htons(port);

  //// bind the server's socket and port.
  if (bind(sock_fd, (const struct sockaddr *) &server_address, sizeof(server_address)) < 0)
  {
    perror("TTFTP_ERROR: bind() fail: ");
    close(sock_fd);
//    return  FAILURE;
  }

  //// Running on infinte loop - except in cases of fatal errors.
  for (;;) {
    client_address_len = sizeof(client_address);
    if ((recieved_msg_size = recvfrom(sock_fd, message_buffer, ECHOMAX, 0, (struct sockaddr *) &client_address,
                                      &client_address_len)) < 0) {
      perror("TTFTP_ERROR: recvfrom() fail: ");
      cerr << "RECVFAIL" << endl;
      continue;
    }
    ////copy the first two bytes of the packet to a variable of 16 bits.
    uint16_t opcode_new = 0;
    memcpy(&opcode_new, message_buffer, OPCODE_LEN);
    //// check if the new opcode is like the wrq.

    if (ntohs(opcode_new) != WRQ_OPCODE) {
      cerr << "FLOWERROR: opcode mismatch with WRQ opcode" << endl;
      cerr << "RECVFAIL" << endl;
      //// in case it isn't, we are still waiting to wrq packet! return to the beginning of the infinite loop
      continue;
    }

    ////in case we got a WRQ, we want to check the file_name of the future data,
    //// and to open a file in our server with the same name.
    //// copy (malloc) the file name by strdup
    char *file_name;
    //// taking the string from the third byte
    file_name = strdup(message_buffer + OPCODE_LEN);
    if (file_name == NULL) {
      cerr << "FLOWERROR: fileName is Null" << endl;
      cerr << "RECVFAIL" << endl;
      continue; //// we are still waiting for good wrq.
    }

    //// open a file according to the file_name we just got:
    int fd_open_file;
    fd_open_file = open(file_name, O_RDWR | O_TRUNC | O_CREAT , 0777);
    if (fd_open_file < 0) {
      //// in case we cant open a file it means there is a system problem, so we end everything
      perror("TTFTP_ERROR: can't open the new file: ");
      cerr << "RECVFAIL" << endl;
      continue;
    }

    //// The file open successfuly, getting the transmission-mode from the client
    char *transmit_mode;
    transmit_mode = strdup(message_buffer + OPCODE_LEN +
        strlen(file_name) + 1); //// taking the string from the end of the file_name
    if (transmit_mode == NULL) {
      cerr << "FLOWERROR: transmit_mode is NULL" << endl;
      cerr << "RECVFAIL" << endl;
      continue; //// we are still waiting for good wrq.
    }

    //// we got an acceptable WRQ
    cout << "IN:WRQ," << file_name << "," << transmit_mode << endl;

    //// from now we dont need the last "mallocs" (we creat a fd for the file)

    free(file_name);
    free(transmit_mode);

    //// after receiving WRQ - sending ACK 0
    struct ACK_packets ack_packet;
    ack_packet.Block_Number_being_acknowledged = htons(ACK_number);
    ack_packet.Opcode = htons(ACK_OPCODE);
    ssize_t buffer_size = sendto(sock_fd, (void *) (&ack_packet), sizeof(ack_packet), 0,
                                 (struct sockaddr *) &client_address, client_address_len);
    if (buffer_size != sizeof(ack_packet)) {
      perror("TTFTP_ERROR: sendto() of ACK fail: ");
      cerr << "RECVFAIL" << endl;
      close(fd_open_file);
      continue;
    }

    //// when we get here - the ACK was sent
    cout << "OUT:ACK," << ACK_number << endl;
    ACK_number++;
    int last_write_size = 0;
        do {
            do {
                do {
                    //  Wait PACKET_TIMEOUT_WAIT to see if something appears
                    //       for us at the socket (we are waiting for DATA)
                    ////reset all the fd bits in the set, make the set of fd we are "listening on" - just sock_fd
                    //// then we will use the range (nfds) and the set in select.
                    fd_set rfds;
                    FD_ZERO(&rfds);
                    FD_SET(sock_fd, &rfds);

          //// set the waiting period
          struct timeval waiting_time_value;
                  waiting_time_value.tv_sec = PACKET_TIMEOUT_WAIT;
                  waiting_time_value.tv_usec = 0;

          //// set the status flag
          data_alert = DATA_ALERT_SUCCESS;

          int select_res = select(sock_fd + 1, &rfds, NULL, NULL, &waiting_time_value);
          if (select_res > 0) {
            ////  if there was something at the socket and
            ////       we are here not because of a timeout
            ////  Read the DATA packet from the socket (at
            ////       least we hope this is a DATA packet)
            if ((recieved_msg_size = recvfrom(sock_fd, message_buffer, ECHOMAX, 0,
                                              (struct sockaddr *) &client_address,
                                              &client_address_len)) < 0) {
              perror("TTFTP_ERROR: recvfrom() DATA fail: ");
              cerr << "RECVFAIL" << endl;
              close(fd_open_file);
//              close(sock_fd);
//              return FAILURE;
            }

            uint16_t opcode_new_data = 0;
            ////copy the first two bytes to a variable of 16 bits.
            memcpy(&opcode_new_data, message_buffer, OPCODE_LEN);

            //// check if the new opcode is like the wrq.

            if (ntohs(opcode_new_data) != DATA_OPCODE) {
              data_alert = DATA_ALERT_OPCODE;
              cout << "FLOWERROR: error in opcode DATA" << endl;
              cout << "RECVFAIL" << endl;
            }

            uint16_t new_data_block_num = 0;
            memcpy(&new_data_block_num, message_buffer + OPCODE_LEN,
                   DATA_BLOCK_NUM_LEN); //// copy two bytes after opcode

            if (ntohs(new_data_block_num) !=
                ACK_number) { //// check if the last ack is equal to the
              // current block_number
              data_alert = DATA_ALERT_BLOCK_NUM;

              cout << "FLOWERROR: the block number is not the last one + 1" <<
              endl;
              cout << "RECVFAIL" << endl;
            }


            //// otherwise is keep being DATA_ALERT_SUCCESS
            if (data_alert == DATA_ALERT_SUCCESS) {
              //// saying we got the DATA
              cout << "IN:DATA," << ntohs(new_data_block_num) << "," << recieved_msg_size << endl;
            }

          }
          if (select_res == 0)
            //  Time out expired while waiting for data to appear at the socket
          {
            // Send another ACK for the last packet
            //// set the status flag
            data_alert = DATA_ALERT_TIMEOUT;
            ////initializing a new ACK packet
            cout << "FLOWERROR: Time expired, waiting for another transmission"
            << endl;
            ack_packet.Block_Number_being_acknowledged = htons(ACK_number - 1);
            ack_packet.Opcode = htons(ACK_OPCODE);
            ssize_t size_buffer = sendto(sock_fd, (void *) (&ack_packet), sizeof(ack_packet), 0,
                                         (struct sockaddr *) &client_address, client_address_len);

            if (size_buffer != sizeof(ack_packet)) {
              perror("TTFTP_ERROR: sendto() of ACK fail: ");
              cerr << "RECVFAIL" << endl;
              close(fd_open_file);
              close(sock_fd);
//              return FAILURE;
            }
            ////The last ACK sent again
            cout << "OUT:ACK," << (ACK_number - 1) << endl;
            timeout_expired_count++;
          }

          if (select_res < 0) {
            perror("TTFTP_ERROR: select() fail: ");
            cerr << "RECVFAIL" << endl;
            close(fd_open_file);
            close(sock_fd);
          }
          //// we start counting in 0 therefor:
          if (timeout_expired_count >= FAILURES_AMOUNT) {
            // FATAL ERROR BAIL OUT
            cerr << "FLOWERROR: re-reception tries over the limit" << endl;
            cerr << "RECVFAIL" << endl;
            data_alert = DATA_ALERT_TIMEOUT_LIMIT;
          }

        } while (data_alert == DATA_ALERT_TIMEOUT);
        // Continue while some socket was ready but recvfrom somehow failed to read the data
        // this text is copied just because it was writen that way in the hw
        if (data_alert == DATA_ALERT_TIMEOUT_LIMIT) //  We got something else but DATA
          //  FATAL ERROR BAIL OUT
        {
          close(fd_open_file);
          timeout_expired_count = 0;
          ACK_number = 0;
        }
        if (data_alert == DATA_ALERT_OPCODE) //  We got something else but DATA
          //  FATAL ERROR BAIL OUT
        {
          close(fd_open_file);
          timeout_expired_count = 0;
          ACK_number = 0;
        }

        if (data_alert ==
            DATA_ALERT_BLOCK_NUM) // The incoming block number is not what we have
          // expected, i.e. this is a DATA pkt but the block number
          // in DATA was wrong (not last ACK’s block number + 1
        {
          // FATAL ERROR BAIL OUT
          close(fd_open_file);
          timeout_expired_count = 0;
          ACK_number = 0;
        }

      } while (false);
      if(data_alert == DATA_ALERT_SUCCESS) {
        //// on this step we need to do all the action in case reading the new packet was succeded:
        //// step 1: write the data to our file:
        int data_actual_size = recieved_msg_size - 4;
        char *data = new char[DATAMAX];
        memcpy(data, message_buffer + HEADER_TFTP, data_actual_size);
        cout << "WRITING:" << data_actual_size << endl;
        last_write_size = write(fd_open_file, (void *) data, data_actual_size);
        if (last_write_size < data_actual_size) {
          perror("TTFTP_ERROR: write() fail: ");
          cerr << "RECVFAIL" << endl;
          free(data);
          close(fd_open_file);
          close(sock_fd);
        }
        timeout_expired_count = 0;
        free(data);

        //// step 1:  send ACK packet to the client:
        ack_packet.Block_Number_being_acknowledged = htons(ACK_number);
        ack_packet.Opcode = htons(ACK_OPCODE);
        ssize_t size_buffer = sendto(sock_fd, (void *) (&ack_packet), sizeof(ack_packet), 0,
                                     (struct sockaddr *) &client_address, client_address_len);
        if (size_buffer != sizeof(ack_packet)) {
          perror("TTFTP_ERROR: sendto() of ACK fail: ");
          cerr << "RECVFAIL" << endl;
          close(fd_open_file);
          close(sock_fd);
        }
        cout << "OUT:ACK," << ACK_number << endl;
        ACK_number++;
      }

    } while (last_write_size == DATAMAX && data_alert == DATA_ALERT_SUCCESS); // Have blocks left to be read from client (not end of transmission)
    if(data_alert == DATA_ALERT_SUCCESS) {
      cout << "RECVOK" << endl;
      ACK_number = 0;
      close(fd_open_file);
    }
  }
  //// this is infinite loop - therefore - unreachable code;

}

bool is_digits(char* str) {
  int i = 0;
  bool retval = true;
  while (str[i] != '\0'){
    if ((str[i] < '0') || (str[i] > '9')){
      retval = false;
      break;
    }
    i++;
  }
  return retval;
}