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
bool delete_file_on_err(int file_desc, char *file_name);

using namespace  std;
int main(int argc, char **argv) {
  int retval = SUCCESS;
  if (argc != 2) {
    cout << "FLOWERROR: Invalid Paramters" << endl;
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

  struct ACK_packets {
    uint16_t Opcode;
    uint16_t Block_Number_being_acknowledged;
  }__attribute__((packed));

  uint16_t  ACK_number=0;

  int timeout_expired_count = 0;
  char message_buffer[ECHOMAX] = {0};
  unsigned int client_address_len;
  int recieved_msg_size;
  int data_alert = DATA_ALERT_SUCCESS;
  struct sockaddr_in server_address, client_address;

  int sock_fd = socket(AF_INET, SOCK_DGRAM, 0);
  if (sock_fd < 0) {
    perror("TTFTP_ERROR: socket() fail: ");
    return  FAILURE;
  }

  memset(&server_address, 0, sizeof(server_address));
  server_address.sin_addr.s_addr = htonl(INADDR_ANY);
  server_address.sin_family = AF_INET;
  server_address.sin_port = htons(port);

  if (bind(sock_fd, (const struct sockaddr *) &server_address, sizeof(server_address)) < 0)
  {
    perror("TTFTP_ERROR: bind() fail: ");
    return FAILURE;
  }
endless_loop:
  for (;;) {
    ACK_number = 0;
    client_address_len = sizeof(client_address);
    if ((recieved_msg_size = recvfrom(sock_fd, message_buffer, ECHOMAX, 0, (struct sockaddr *) &client_address,
                                      &client_address_len)) < 0) {
      perror("TTFTP_ERROR: recvfrom() fail: ");
      cerr << "RECVFAIL" << endl;
      return FAILURE;
    }
    uint16_t opcode_new = 0;
    memcpy(&opcode_new, message_buffer, OPCODE_LEN);

    if (ntohs(opcode_new) != WRQ_OPCODE) {
      cerr << "FLOWERROR: opcode mismatch with WRQ opcode" << endl;
      cerr << "RECVFAIL" << endl;
      continue;
    }

    char *file_name;
    file_name = strdup(message_buffer + OPCODE_LEN);
    if (file_name == NULL) {
      cerr << "FLOWERROR: fileName is Null" << endl;
      cerr << "RECVFAIL" << endl;
      continue;
    }

    int fd_open_file;
    fd_open_file = open(file_name, O_RDWR | O_TRUNC | O_CREAT , 0777);
    if (fd_open_file < 0) {
      perror("TTFTP_ERROR: can't open the new file: ");
      cerr << "RECVFAIL" << endl;
      return FAILURE;
    }

    char *transmit_mode;
    transmit_mode = strdup(message_buffer + OPCODE_LEN +
        strlen(file_name) + 1);
    if (transmit_mode != NULL) {
      if (strcmp(transmit_mode, "octet")) {
        cerr << "FLOWERROR: transmit_mode is not octet" << endl;
        cerr << "RECVFAIL" << endl;
        if (!delete_file_on_err(fd_open_file, file_name)){
          return FAILURE;
        }
        continue;
      }
    }
    if (transmit_mode == NULL) {
        cerr << "FLOWERROR: transmit_mode is NULL" << endl;
        cerr << "RECVFAIL" << endl;
        if (!delete_file_on_err(fd_open_file, file_name)){
          return FAILURE;
        }
        continue;
    }

    cout << "IN:WRQ," << file_name << "," << transmit_mode << endl;

    free(transmit_mode);

    struct ACK_packets ack_packet;
    ack_packet.Block_Number_being_acknowledged = htons(ACK_number);
    ack_packet.Opcode = htons(ACK_OPCODE);
    ssize_t buffer_size = sendto(sock_fd, (void *) (&ack_packet), sizeof(ack_packet), 0,
                                 (struct sockaddr *) &client_address, client_address_len);
    if (buffer_size == -1){
      delete_file_on_err(fd_open_file, file_name);
      perror("TTFTP_ERROR: sendto() of ACK fail: ");
      cerr << "RECVFAIL" << endl;
      return FAILURE;
    }
    if (buffer_size != sizeof(ack_packet)) {
      perror("TTFTP_ERROR: sendto() of ACK fail: ");
      cerr << "RECVFAIL" << endl;
      delete_file_on_err(fd_open_file, file_name);
      return FAILURE;
    }

    cout << "OUT:ACK," << ACK_number << endl;
    ACK_number++;
    int last_write_size = 0;
        do {
            do {
                do {
                    fd_set rfds;
                    FD_ZERO(&rfds);
                    FD_SET(sock_fd, &rfds);

          struct timeval waiting_time_value;
                  waiting_time_value.tv_sec = PACKET_TIMEOUT_WAIT;
                  waiting_time_value.tv_usec = 0;

          data_alert = DATA_ALERT_SUCCESS;

          int select_res = select(sock_fd + 1, &rfds, NULL, NULL, &waiting_time_value);
          if (select_res > 0) {
            if ((recieved_msg_size = recvfrom(sock_fd, message_buffer, ECHOMAX, 0,
                                              (struct sockaddr *) &client_address,
                                              &client_address_len)) < 0) {
              perror("TTFTP_ERROR: recvfrom() DATA fail: ");
              cerr << "RECVFAIL" << endl;
              delete_file_on_err(fd_open_file, file_name);
              return FAILURE;
            }

            uint16_t opcode_new_data = 0;
            memcpy(&opcode_new_data, message_buffer, OPCODE_LEN);


            if (ntohs(opcode_new_data) != DATA_OPCODE) {
              data_alert = DATA_ALERT_OPCODE;
              cout << "FLOWERROR: error in opcode DATA" << endl;
              cout << "RECVFAIL" << endl;
              if (!delete_file_on_err(fd_open_file, file_name)){
                return FAILURE;
              }
              goto endless_loop;
            }

            uint16_t new_data_block_num = 0;
            memcpy(&new_data_block_num, message_buffer + OPCODE_LEN,
                   DATA_BLOCK_NUM_LEN);

            if (ntohs(new_data_block_num) != ACK_number) {
              data_alert = DATA_ALERT_BLOCK_NUM;
              cout << "FLOWERROR: the block number is not the last one + 1" <<
              endl;
              cout << "RECVFAIL" << endl;
              if (!delete_file_on_err(fd_open_file, file_name)){
                return FAILURE;
              }
              goto endless_loop;
            }


            if (data_alert == DATA_ALERT_SUCCESS) {
              cout << "IN:DATA," << ntohs(new_data_block_num) << "," << recieved_msg_size << endl;
            }

          }
          if (select_res == 0)
          {
            data_alert = DATA_ALERT_TIMEOUT;
            cout << "FLOWERROR: Time expired, waiting for another transmission"
            << endl;
            ack_packet.Block_Number_being_acknowledged = htons(ACK_number - 1);
            ack_packet.Opcode = htons(ACK_OPCODE);
            ssize_t size_buffer = sendto(sock_fd, (void *) (&ack_packet), sizeof(ack_packet), 0,
                                         (struct sockaddr *) &client_address, client_address_len);
            if (size_buffer == -1){
              perror("TTFTP_ERROR: sendto() of ACK fail: ");
              cerr << "RECVFAIL" << endl;
              delete_file_on_err(fd_open_file, file_name);
              return FAILURE;
            }
            if (size_buffer != sizeof(ack_packet)) {
              perror("TTFTP_ERROR: sendto() of ACK fail: ");
              cerr << "RECVFAIL" << endl;
              if (!delete_file_on_err(fd_open_file, file_name)){
                return FAILURE;
              }
            }
            cout << "OUT:ACK," << (ACK_number - 1) << endl;
            timeout_expired_count++;
          }

          if (select_res < 0) {
            perror("TTFTP_ERROR: select() fail: ");
            cerr << "RECVFAIL" << endl;
            delete_file_on_err(fd_open_file, file_name);
            return FAILURE;
          }
          if (timeout_expired_count >= FAILURES_AMOUNT) {
            cerr << "FLOWERROR: re-reception tries over the limit" << endl;
            cerr << "RECVFAIL" << endl;
            data_alert = DATA_ALERT_TIMEOUT_LIMIT;
            if (!delete_file_on_err(fd_open_file, file_name)){
              return FAILURE;
            }
            goto endless_loop;
          }

        } while (data_alert == DATA_ALERT_TIMEOUT); //while #1
        if (data_alert == DATA_ALERT_TIMEOUT_LIMIT)
        {
          if (!delete_file_on_err(fd_open_file, file_name)){
            return FAILURE;
          }
          timeout_expired_count = 0;
          ACK_number = 0;
          goto endless_loop;
        }
        if (data_alert == DATA_ALERT_OPCODE)
        {
          if (!delete_file_on_err(fd_open_file, file_name)){
            return FAILURE;
          }
          timeout_expired_count = 0;
          ACK_number = 0;
          goto endless_loop;
        }

        if (data_alert ==
            DATA_ALERT_BLOCK_NUM)
        {
          if (!delete_file_on_err(fd_open_file, file_name)){
            return FAILURE;
          }
          timeout_expired_count = 0;
          ACK_number = 0;
          goto endless_loop;
        }

      } while (false); //while #2
      if(data_alert == DATA_ALERT_SUCCESS) {
        int data_actual_size = recieved_msg_size - 4;
        char *data = new char[DATAMAX];
        memcpy(data, message_buffer + HEADER_TFTP, data_actual_size);
        cout << "WRITING:" << data_actual_size << endl;
        last_write_size = write(fd_open_file, (void *) data, data_actual_size);
        if (last_write_size == -1){
          perror("TTFTP_ERROR: write() fail: ");
          cerr << "RECVFAIL" << endl;
          delete_file_on_err(fd_open_file, file_name);
          return FAILURE;
        }
        if (last_write_size < data_actual_size) {
          perror("TTFTP_ERROR: write() fail: ");
          cerr << "RECVFAIL" << endl;
          free(data);
          if (!delete_file_on_err(fd_open_file, file_name)){
            return FAILURE;
          }
        }
        timeout_expired_count = 0;
        free(data);

        ack_packet.Block_Number_being_acknowledged = htons(ACK_number);
        ack_packet.Opcode = htons(ACK_OPCODE);
        ssize_t size_buffer = sendto(sock_fd, (void *) (&ack_packet), sizeof(ack_packet), 0,
                                     (struct sockaddr *) &client_address, client_address_len);
        if (size_buffer == -1){
          perror("TTFTP_ERROR: sendto() of ACK fail: ");
          cerr << "RECVFAIL" << endl;
          delete_file_on_err(fd_open_file, file_name);
          return FAILURE;
        }
        if (size_buffer != sizeof(ack_packet)) {
          perror("TTFTP_ERROR: sendto() of ACK fail: ");
          cerr << "RECVFAIL" << endl;
          if (!delete_file_on_err(fd_open_file, file_name)){
            return FAILURE;
          }
          goto endless_loop;
        }
        cout << "OUT:ACK," << ACK_number << endl;
        ACK_number++;
      }

    } while (last_write_size == DATAMAX && data_alert == DATA_ALERT_SUCCESS);
        //while #3
    if(data_alert == DATA_ALERT_SUCCESS) {
      cout << "RECVOK" << endl;
      ACK_number = 0;
      if (close(fd_open_file)){
        perror("close file failed");
        return FAILURE;
      }
    }
  }
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

bool delete_file_on_err(int file_desc, char *file_name){
  if (close(file_desc) == -1){
    perror("close file failed");
    return false;
  }
  if (remove(file_name) == -1){
    perror("remove file failed");
    return false;
  }
  return true;
}

void clean_run (){

}