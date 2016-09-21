#!/bin/bash
# Copyright (c) 2013 The Chromium OS Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

readonly PREREQS=(build-essential protobuf-compiler libprotobuf-dev openssl \
  libgtest-dev)

# Install quipper pre-requisites.
sudo apt-get install ${PREREQS[@]}
