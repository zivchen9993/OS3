#!/bin/bash

nc -w0 -u 127.0.0.1 20000 < wrq_packet_file_name_s
nc -w0 -u 127.0.0.1 20000 < data1
nc -w0 -u 127.0.0.1 20000 < data2
nc -w0 -u 127.0.0.1 20000 < data3
nc -w0 -u 127.0.0.1 20000 < data4
nc -w0 -u 127.0.0.1 20000 < data5
nc -w0 -u 127.0.0.1 20000 < data6
nc -w0 -u 127.0.0.1 20000 < data7
nc -w0 -u 127.0.0.1 20000 < data8