# Copyright 2018 Google Inc. All Rights Reserved.
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

#!/bin/bash -x

# This is a script that generates the test MacOS executables in this directory.
# It should be needed very rarely to run this script. It is mostly provided
# as a future reference on how the original binary set was created.

set -o errexit

cat <<EOF >/tmp/hello.cc
#include <stdio.h>

int main() {
  printf("Hello, world!\n");
  return 0;
}
EOF

cat <<EOF >/tmp/lib.c
int foo() {
  return 1;
}

int bar() {
  return 2;
}
EOF

cd $(dirname $0)
rm -rf exe_mac_64* lib_mac_64*
clang -g -o exe_mac_64 /tmp/hello.c
clang -g -o lib_mac_64 -dynamiclib /tmp/lib.c
