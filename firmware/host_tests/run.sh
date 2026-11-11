#!/bin/sh
# Builds components/core for the Mac and runs its unit tests.
set -e
cd "$(dirname "$0")"
mkdir -p build
cc -std=c11 -Wall -Wextra -Werror -g -fsanitize=address,undefined \
   -I ../components/core -o build/host_tests *.c ../components/core/*.c -lm
./build/host_tests
