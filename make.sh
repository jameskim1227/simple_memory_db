#! /bin/bash -x
gcc -o smd_server smd_server.c -Wall -I./ -I./include/apr-1/ -L./lib -lapr-1

gcc -o smd_client smd_client.c -Wall
