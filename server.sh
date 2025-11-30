#!/usr/bin/bash
set xev
#clang -O3 -march=native -flto -Wl, -g0 -o echo main.c http.c

clang \
  -O3 -march=native -flto=thin \
  -fomit-frame-pointer \
  -fdata-sections -ffunction-sections \
  -DNDEBUG \
  main.c http.c \
  -Wl,-O2 -Wl,--gc-sections -s \
  -o echo

if [ $? = 0 ]; then
    ./echo
fi
