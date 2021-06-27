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

#define WAIT_FOR_PACKET_TIMEOUT 3
#define NUMBER_OF_FAILURES 7
#define ECHOMAX 516
#define DATA_LENGTH 512
#define OPCODE_LENGTH 2
#define DATA_BLOCK_NUM_LENGTH 2
#define OPCODE_WRQ 2
#define OPCODE_ACK 4
#define OPCODE_DATA 3
#define FAILURE 1
#define ALERT_DATA_TIMEOUT_LIMIT 4
#define ALERT_DATA_TIMEOUT 1
#define ALERT_DATA_OPCODE 2
#define ALERT_DATA_BLOCK_NUM 3
#define ALERT_DATA_SUCCESS 0
#define HEADER_TFTP 4
#define SUCCESS 0

bool is_digits(char* str);

using namespace  std;
int main(int argc, char **argv) {
  //TODO: wrong opcode should print only once
  //// checking there is just one input argument - the port number.
  int retval = SUCCESS;
  if (argc != 2) {
    cout << "FLOWERROR: Invalid Paramters" << endl; // TODO: maybe check if longer than short int port is int
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
  struct ACK_packet {
    uint16_t Opcode;
    uint16_t Block_Number_being_acknowledged;
  }__attribute__((packed));

  uint16_t  ACK_NUM=0;

  //// declaring variables for future use
  int timeoutExpiredCount = 0;
  char messageBuffer[ECHOMAX] = {0};
  unsigned int client_address_length;
  int RecievedMsgSize;
  int Alert_Data = ALERT_DATA_SUCCESS;
  struct sockaddr_in server_address, client_address;

  ////open socket for server
  int sockfd = socket(AF_INET, SOCK_DGRAM, 0); // GOOD
  if (sockfd < 0) {
    perror("TTFTP_ERROR: socket() fail: ");
    return  FAILURE;
  }

  //// initializing the server parameters
  memset(&server_address, 0, sizeof(server_address));
  server_address.sin_addr.s_addr = htonl(INADDR_ANY);
  server_address.sin_family = AF_INET;
  server_address.sin_port = htons(port);

  //// bind the server's socket and port.
  if (bind(sockfd, (const struct sockaddr *) &server_address, sizeof(server_address)) < 0)
  {
    perror("TTFTP_ERROR: bind() fail: ");
    close(sockfd);
//    return  FAILURE;
  }

  //// Running on infinte loop - except in cases of fatal errors.
  for (;;) {
    client_address_length = sizeof(client_address);
    if ((RecievedMsgSize = recvfrom(sockfd, messageBuffer, ECHOMAX, 0, (struct sockaddr *) &client_address,
                                    &client_address_length)) < 0) {
      perror("TTFTP_ERROR: recvfrom() fail: ");
      cerr << "RECVFAIL" << endl;
//      close(sockfd);
//      return FAILURE;
      continue;
    }
    ////copy the first two bytes of the packet to a variable of 16 bits.
    uint16_t new_opcode = 0;
    memcpy(&new_opcode, messageBuffer, OPCODE_LENGTH);
    //// check if the new opcode is like the wrq.

    if (ntohs(new_opcode) != OPCODE_WRQ) {
      cerr << "FLOWERROR: opcode mismatch with WRQ opcode" << endl;
      cerr << "RECVFAIL" << endl;
      //// in case it isn't, we are still waiting to wrq packet! return to the beginning of the infinite loop
//      return FAILURE;
      continue;
    }

    ////in case we got a WRQ, we want to check the file_name of the future data,
    //// and to open a file in our server with the same name.
    //// copy (malloc) the file name by strdup
    char *filename;
    //// taking the string from the third byte
    filename = strdup(messageBuffer + OPCODE_LENGTH);
    if (filename == NULL) {
      cerr << "FLOWERROR: fileName is Null" << endl;
      cerr << "RECVFAIL" << endl;
//      free(filename); //// in case there is something there (supposed to be empty)
      continue; //// we are still waiting for good wrq.
    }

    //// open a file according to the filename we just got:
    int fd_opened_file;
    fd_opened_file = open(filename, O_RDWR | O_TRUNC | O_CREAT , 0777);
    if (fd_opened_file < 0) {
      //// in case we cant open a file it means there is a system problem, so we end everything
      perror("TTFTP_ERROR: can't open the new file: ");
      cerr << "RECVFAIL" << endl;
//      close(sockfd);
//      free(filename);
//      return FAILURE;
      continue;
    }

    //// The file open successfuly, getting the transmission-mode from the client
    char *transmission_mode;
    transmission_mode = strdup(messageBuffer + OPCODE_LENGTH +
        strlen(filename) + 1); //// taking the string from the end of the filename
    if (transmission_mode == NULL) {
      cerr << "FLOWERROR: transmission_mode is NULL" << endl;
      cerr << "RECVFAIL" << endl;
//      free(filename);
//      free(transmission_mode);//// in case there is something there (supposed to be empty)
//      close(fd_opened_file);
      continue; //// we are still waiting for good wrq.
    }

    //// we got an acceptable WRQ
    cout << "IN:WRQ," << filename << "," << transmission_mode << endl;

    //// from now we dont need the last "mallocs" (we creat a fd for the file)

    free(filename);
    free(transmission_mode);

    //// after receiving WRQ - sending ACK 0
    struct ACK_packet ackpacket;
    ackpacket.Block_Number_being_acknowledged = htons(ACK_NUM);
    ackpacket.Opcode = htons(OPCODE_ACK);
    ssize_t size_buffer = sendto(sockfd, (void *) (&ackpacket), sizeof(ackpacket), 0,
                                 (struct sockaddr *) &client_address, client_address_length);
    if (size_buffer != sizeof(ackpacket)) {
      perror("TTFTP_ERROR: sendto() of ACK fail: ");
      cerr << "RECVFAIL" << endl;
      close(fd_opened_file);
//      close(sockfd);
//      return FAILURE;
      continue;
    }

    //// when we get here - the ACK was sent
    cout << "OUT:ACK," << ACK_NUM << endl;
    ACK_NUM++;
    int lastWriteSize = 0;

        do {
            do {
                do {
                    //  Wait WAIT_FOR_PACKET_TIMEOUT to see if something appears
                    //       for us at the socket (we are waiting for DATA)
                    ////reset all the fd bits in the set, make the set of fd we are "listening on" - just sockfd
                    //// then we will use the range (nfds) and the set in select.
                    fd_set rfds;
                    FD_ZERO(&rfds);
                    FD_SET(sockfd, &rfds);

          //// set the waiting period
          struct timeval waiting_time_val;
          waiting_time_val.tv_sec = WAIT_FOR_PACKET_TIMEOUT;
          waiting_time_val.tv_usec = 0;

          //// set the status flag
          Alert_Data = ALERT_DATA_SUCCESS;

          int select_result = select(sockfd + 1, &rfds, NULL, NULL, &waiting_time_val);
          if (select_result > 0) {
            ////  if there was something at the socket and
            ////       we are here not because of a timeout
            ////  Read the DATA packet from the socket (at
            ////       least we hope this is a DATA packet)
            if ((RecievedMsgSize = recvfrom(sockfd, messageBuffer, ECHOMAX, 0,
                                            (struct sockaddr *) &client_address,
                                            &client_address_length)) < 0) {
              perror("TTFTP_ERROR: recvfrom() DATA fail: ");
              cerr << "RECVFAIL" << endl;
              close(fd_opened_file);
              close(sockfd);
              return FAILURE;

            }

            uint16_t new_data_opcode = 0;
            ////copy the first two bytes to a variable of 16 bits.
            memcpy(&new_data_opcode, messageBuffer, OPCODE_LENGTH);

            //// check if the new opcode is like the wrq.

            if (ntohs(new_data_opcode) != OPCODE_DATA) {
              Alert_Data = ALERT_DATA_OPCODE;
              cout << "FLOWERROR: error in opcode DATA" << endl;
              cout << "RECVFAIL" << endl;
            }

            uint16_t new_data_num_block = 0;
            memcpy(&new_data_num_block, messageBuffer + OPCODE_LENGTH,
                   DATA_BLOCK_NUM_LENGTH); //// copy two bytes after opcode

            if (ntohs(new_data_num_block) !=
                ACK_NUM) { //// check if the last ack is equal to the
              // current block_number
              Alert_Data = ALERT_DATA_BLOCK_NUM;

              cout << "FLOWERROR: the block number is not the last one + 1" <<
              endl;
              cout << "RECVFAIL" << endl;
//            close(fd_opened_file);
//            close(sockfd);
//            return FAILURE;
            }


            //// otherwise is keep being ALERT_DATA_SUCCESS
            if (Alert_Data == ALERT_DATA_SUCCESS) {
              //// saying we got the DATA
              cout << "IN:DATA," << ntohs(new_data_num_block) << "," << RecievedMsgSize << endl;
            }

          }
          if (select_result == 0)
            //  Time out expired while waiting for data to appear at the socket
          {
            // Send another ACK for the last packet
            //// set the status flag
            Alert_Data = ALERT_DATA_TIMEOUT;
            ////initializing a new ACK packet
            cout << "FLOWERROR: Time expired, waiting for another transmission"
            << endl;
            ackpacket.Block_Number_being_acknowledged = htons(ACK_NUM - 1);
            ackpacket.Opcode = htons(OPCODE_ACK);
            ssize_t size_buffer = sendto(sockfd, (void *) (&ackpacket), sizeof(ackpacket), 0,
                                         (struct sockaddr *) &client_address, client_address_length);

            if (size_buffer != sizeof(ackpacket)) {
              perror("TTFTP_ERROR: sendto() of ACK fail: ");
              cerr << "RECVFAIL" << endl;
              close(fd_opened_file);
              close(sockfd);
//              return FAILURE;
            }
            ////The last ACK sent again
            cout << "OUT:ACK," << (ACK_NUM - 1) << endl;

            timeoutExpiredCount++;
          }

          if (select_result < 0) {
            perror("TTFTP_ERROR: select() fail: ");
            cerr << "RECVFAIL" << endl;
            close(fd_opened_file);
            close(sockfd);
//            return FAILURE;
          }
          //// we start counting in 0 therefor:
          if (timeoutExpiredCount >= NUMBER_OF_FAILURES) {
            // FATAL ERROR BAIL OUT
            cerr << "FLOWERROR: re-reception tries over the limit" << endl;
            cerr << "RECVFAIL" << endl;
            Alert_Data = ALERT_DATA_TIMEOUT_LIMIT;
          }

        } while (Alert_Data == ALERT_DATA_TIMEOUT);
        // Continue while some socket was ready but recvfrom somehow failed to read the data
        // this text is copied just because it was writen that way in the hw
        if (Alert_Data == ALERT_DATA_TIMEOUT_LIMIT) //  We got something else but DATA
          //  FATAL ERROR BAIL OUT
        {
          close(fd_opened_file);
          timeoutExpiredCount = 0;
          ACK_NUM = 0;
        }
        if (Alert_Data == ALERT_DATA_OPCODE) //  We got something else but DATA
          //  FATAL ERROR BAIL OUT
        {
          close(fd_opened_file);
          timeoutExpiredCount = 0;
          ACK_NUM = 0;
        }

        if (Alert_Data ==
            ALERT_DATA_BLOCK_NUM) // The incoming block number is not what we have
          // expected, i.e. this is a DATA pkt but the block number
          // in DATA was wrong (not last ACK’s block number + 1
        {
          // FATAL ERROR BAIL OUT
          close(fd_opened_file);
          timeoutExpiredCount = 0;
          ACK_NUM = 0;
        }

      } while (false);
      if(Alert_Data == ALERT_DATA_SUCCESS) {
        //// on this step we need to do all the action in case reading the new packet was succeded:
        //// step 1: write the data to our file:
        int actual_data_size = RecievedMsgSize - 4;
        char *DATA = new char[DATA_LENGTH];
        memcpy(DATA, messageBuffer + HEADER_TFTP, actual_data_size);
        cout << "WRITING:" << actual_data_size << endl;
        lastWriteSize = write(fd_opened_file, (void *) DATA, actual_data_size);
        if (lastWriteSize < actual_data_size) {
          perror("TTFTP_ERROR: write() fail: ");
          cerr << "RECVFAIL" << endl;
          free(DATA);
          close(fd_opened_file);
          close(sockfd);
//          return FAILURE;
        }
        timeoutExpiredCount = 0;
        free(DATA);

        //// step 1:  send ACK packet to the client:
        ackpacket.Block_Number_being_acknowledged = htons(ACK_NUM);
        ackpacket.Opcode = htons(OPCODE_ACK);
        ssize_t size_buffer = sendto(sockfd, (void *) (&ackpacket), sizeof(ackpacket), 0,
                                     (struct sockaddr *) &client_address, client_address_length);
        if (size_buffer != sizeof(ackpacket)) {
          perror("TTFTP_ERROR: sendto() of ACK fail: ");
          cerr << "RECVFAIL" << endl;
          close(fd_opened_file);
          close(sockfd);
//          return FAILURE;
        }
        cout << "OUT:ACK," << ACK_NUM << endl;
        ACK_NUM++;
      }

    } while (lastWriteSize == DATA_LENGTH && Alert_Data == ALERT_DATA_SUCCESS); // Have blocks left to be read from client (not end of transmission)
    if(Alert_Data == ALERT_DATA_SUCCESS) {
      cout << "RECVOK" << endl;
      ACK_NUM = 0;
      close(fd_opened_file);
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
//  return std::all_of(str.begin(), str.end(), ::isdigit);
}