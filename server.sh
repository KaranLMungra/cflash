#!/usr/bin/bash
set xev
clang -O3 -march=native -flto -Wl,--strip-all -g0 -o echo main.c http.c

if [ $? = 0 ]; then
    ./echo
fi
