#!/usr/bin/bash
set xev
#clang -O3 -march=native -flto -Wl, -g0 -o echo main.c http.c

clang \
  -O3 -march=native -flto=thin \
  -fomit-frame-pointer \
  -fno-stack-protector \
  -fno-asynchronous-unwind-tables -fno-unwind-tables \
  -DNDEBUG \
  main.c http.c \
  -o echo

if [ $? = 0 ]; then
    ./echo
fi
