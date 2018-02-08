// Copyright (c) 2013 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stdlib.h>
#include <unistd.h>

#include <string>
#include <vector>

#include "base/logging.h"

#include "compat/log_level.h"
#include "compat/string.h"
#include "conversion_utils.h"

using quipper::FormatAndFile;
using quipper::kPerfFormat;
using quipper::kProtoTextFormat;

namespace {
// Default output format of this tool is proto text format.
const char kDefaultOutputFormat[] = "text";

// Default output filename is simply stdout.
const char kDefaultOutputFilename[] = "/dev/stdout";

// Default input filename is perf.data in cwd.
const char kDefaultInputFilename[] = "perf.data";

// Default input format is perf.data format.
const char kDefaultInputFormat[] = "perf";

// Parses arguments, storing the results in |input| and |output|. Returns true
// if arguments parsed successfully and false otherwise.
bool ParseArguments(int argc, char* argv[], FormatAndFile* input,
                    FormatAndFile* output) {
  output->filename = kDefaultOutputFilename;
  output->format = kDefaultOutputFormat;
  input->filename = kDefaultInputFilename;
  input->format = kDefaultInputFormat;

  int opt;
  while ((opt = getopt(argc, argv, "i:o:I:O:v:")) != -1) {
    switch (opt) {
      case 'i': {
        input->filename = optarg;
        break;
      }
      case 'I': {
        input->format = optarg;
        break;
      }
      case 'o': {
        output->filename = optarg;
        break;
      }
      case 'O': {
        output->format = optarg;
        break;
      }
      case 'v': {
        quipper::SetVerbosityLevel(atoi(optarg));
        break;
      }
      default:
        return false;
    }
  }
  return true;
}

void PrintUsage() {
  LOG(INFO) << "Usage:";
  LOG(INFO) << "<exe> -i <input filename> -I <input format>"
            << " -o <output filename> -O <output format> -v <verbosity level>";
  LOG(INFO) << "Format options are: '" << kPerfFormat << "' for perf.data"
            << " and '" << kProtoTextFormat << "' for proto text.";
  LOG(INFO) << "By default it reads from perf.data and outputs to /dev/stdout"
            << " in proto text format.";
  LOG(INFO) << "Default verbosity level is 0. Higher values increase verbosity."
            << " Negative values filter LOG() levels.";
}
}  // namespace

int main(int argc, char* argv[]) {
  FormatAndFile input, output;
  if (!ParseArguments(argc, argv, &input, &output)) {
    PrintUsage();
    return EXIT_FAILURE;
  }

  if (!quipper::ConvertFile(input, output)) return EXIT_FAILURE;
  return EXIT_SUCCESS;
}
