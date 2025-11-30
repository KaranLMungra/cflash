#!/usr/bin/bash
clang -o echo echo_server.c http.c

if [ $? = 0 ]; then
    ./echo
fi
