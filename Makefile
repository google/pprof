# Copyright (c) 2016, Google Inc.
# All rights reserved.
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions are met:
#     * Redistributions of source code must retain the above copyright
#       notice, this list of conditions and the following disclaimer.
#     * Redistributions in binary form must reproduce the above copyright
#       notice, this list of conditions and the following disclaimer in the
#       documentation and/or other materials provided with the distribution.
#     * Neither the name of Google Inc. nor the
#       names of its contributors may be used to endorse or promote products
#       derived from this software without specific prior written permission.
#
# THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
# ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
# WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
# DISCLAIMED. IN NO EVENT SHALL Google Inc. BE LIABLE FOR ANY
# DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
# (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
# LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
# ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
# (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
# SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
#
################################################################################

CXX ?= g++

BASE_VER ?= 369476
PKG_CONFIG ?= pkg-config
PC_DEPS = openssl protobuf
PC_CFLAGS := $(shell $(PKG_CONFIG) --cflags $(PC_DEPS))
PC_LIBS := $(shell $(PKG_CONFIG) --libs $(PC_DEPS))
CWP = third_party/chromiumos-wide-profiling

CXXFLAGS += -std=c++11 -g -Wall -Werror -Wall -Wno-error
CPPFLAGS += -Icompat -I${CWP}/mybase \
						-I${CWP}/compat/ext \
						-I${CWP} \
						-Ithird_party \
						-I. -I.. $(PC_CFLAGS)
LDLIBS += -lelf -lpthread -lgcov -lgtest $(PC_LIBS)

QUIPPER_PROGRAMS = quipper perf_converter
CONVERTER_PROGRAMS = perf_to_profile
QUIPPER_MAIN_SOURCES = $(QUIPPER_PROGRAMS:%=${CWP}/%.cc)
CONVERTER_MAIN_SOURCES = $(CONVERTER_PROGRAMS:%=%.cc)
MAIN_SOURCES = $(QUIPPER_MAIN_SOURCES) $(CONVERTER_MAIN_SOURCES)
MAIN_OBJECTS = $(MAIN_SOURCES:%.cc=%.o)
PROGRAMS = $(QUIPPER_PROGRAMS) $(CONVERTER_PROGRAMS)

QUIPPER_LIBRARY_SOURCES = address_mapper.cc buffer_reader.cc \
	buffer_writer.cc conversion_utils.cc data_reader.cc data_writer.cc \
	file_reader.cc mybase/base/logging.cc perf_option_parser.cc \
	perf_data_utils.cc perf_parser.cc perf_protobuf_io.cc perf_reader.cc \
	perf_recorder.cc perf_serializer.cc perf_stat_parser.cc \
	run_command.cc sample_info_reader.cc scoped_temp_path.cc utils.cc \
	compat/ext/detail/log_level.cc dso.cc huge_pages_mapping_deducer.cc
QUIPPER_LIBRARY_SOURCES := \
	$(QUIPPER_LIBRARY_SOURCES:%=${CWP}/%)
CONVERTER_LIBRARY_SOURCES = perf_data_converter.cc perf_data_handler.cc \
														builder.cc chrome_huge_pages_mapping_deducer.cc
LIBRARY_SOURCES = $(QUIPPER_LIBRARY_SOURCES) $(CONVERTER_LIBRARY_SOURCES)

QUIPPER_PROTOS = perf_data.proto perf_stat.proto
QUIPPER_PROTOS := $(QUIPPER_PROTOS:%=${CWP}/%)
CONVERTER_PROTOS = profile.proto profile_wrappers.proto
QUIPPER_GENERATED_SOURCES = $(QUIPPER_PROTOS:.proto=.pb.cc)
QUIPPER_GENERATED_HEADERS = $(QUIPPER_PROTOS:.proto=.pb.h)
GENERATED_SOURCES = $(CONVERTER_PROTOS:.proto=.pb.cc) \
	$(QUIPPER_GENERATED_SOURCES)
GENERATED_HEADERS = $(CONVERTER_PROTOS:.proto=.pb.h) \
	$(QUIPPER_GENERATED_HEADERS)

QUIPPER_COMMON_SOURCES = $(QUIPPER_LIBRARY_SOURCES) \
												 $(QUIPPER_GENERATED_SOURCES)
QUIPPER_COMMON_OBJECTS = $(QUIPPER_COMMON_SOURCES:.cc=.o)

COMMON_SOURCES = $(GENERATED_SOURCES) $(LIBRARY_SOURCES)
COMMON_OBJECTS = $(COMMON_SOURCES:.cc=.o)

TEST_COMMON_SOURCES = \
	${CWP}/perf_test_files.cc \
  ${CWP}/test_perf_data.cc \
  ${CWP}/test_utils.cc
TEST_COMMON_OBJECTS = $(TEST_COMMON_SOURCES:.cc=.o)

INTEGRATION_TEST_SOURCES = ${CWP}/conversion_utils_test.cc
PERF_RECORDER_TEST_SOURCES = ${CWP}/perf_recorder_test.cc
QUIPPER_UNIT_TEST_SOURCES = \
	address_mapper_test.cc buffer_reader_test.cc buffer_writer_test.cc \
	file_reader_test.cc perf_data_utils_test.cc perf_option_parser_test.cc \
	perf_parser_test.cc perf_reader_test.cc perf_serializer_test.cc \
	perf_stat_parser_test.cc run_command_test.cc \
	sample_info_reader_test.cc scoped_temp_path_test.cc utils_test.cc \
	dso_test_utils.cc huge_pages_mapping_deducer_test.cc
QUIPPER_UNIT_TEST_SOURCES := \
	$(QUIPPER_UNIT_TEST_SOURCES:%=${CWP}/%)

CONVERTER_UNIT_TESTS = intervalmap_test.cc perf_data_converter_test.cc \
	perf_data_handler_test.cc chrome_huge_pages_mapping_deducer_test.cc
CONVERTER_UNIT_TEST_BINARIES = $(CONVERTER_UNIT_TESTS:.cc=)

TEST_SOURCES = $(INTEGRATION_TEST_SOURCES) $(PERF_RECORDER_TEST_SOURCES) \
	       $(QUIPPER_UNIT_TEST_SOURCES) $(TEST_COMMON_SOURCES) \
				 ${CWP}/test_runner.cc $(CONVERTER_UNIT_TESTS)
TEST_OBJECTS = $(TEST_SOURCES:.cc=.o)
TEST_INTERMEDIATES = $(TEST_SOURCES:.cc=.d*)
TEST_BINARIES = unit_tests $(CONVERTER_UNIT_TEST_BINARIES) perf_recorder_test \
								integration_tests

ALL_SOURCES = $(MAIN_SOURCES) $(COMMON_SOURCES) $(TEST_SOURCES)

INTERMEDIATES = $(ALL_SOURCES:.cc=.d*)

all: $(PROGRAMS)
	@echo Sources compiled!

ifneq ($(MAKECMDGOALS),clean)
  -include $(ALL_SOURCES:.cc=.d)
endif

# Taken from:
# http://www.gnu.org/software/make/manual/make.html#Automatic-Prerequisites
%.d: %.cc $(GENERATED_HEADERS)
	@set -e; rm -f $@; \
	$(CXX) -MM $(CPPFLAGS) $(CXXFLAGS) $< > $@.$$$$; \
	sed 's,\($*\)\.o[ :]*,\1.o $@ : ,g' < $@.$$$$ > $@; \
	rm -f $@.$$$$

# Rule for compiling protobufs.
%.pb.h %.pb.cc: %.proto
	protoc --cpp_out=. $^

# Do not remove protobuf headers that were generated as dependencies of other
# modules.
.SECONDARY: $(GENERATED_HEADERS)

$(QUIPPER_PROGRAMS): %: ${CWP}/%.o $(QUIPPER_COMMON_OBJECTS)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -o $@ $^ $(LDFLAGS) $(LDLIBS)

$(CONVERTER_PROGRAMS): %: %.o $(COMMON_OBJECTS)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -o $@ $^ $(LDFLAGS) $(LDLIBS)

INTEGRATION_TEST_OBJECTS = $(INTEGRATION_TEST_SOURCES:.cc=.o) ${CWP}/test_runner.o
integration_tests: LDLIBS += -lgtest -lelf -lcap
integration_tests: %: $(COMMON_OBJECTS) $(TEST_COMMON_OBJECTS) $(INTEGRATION_TEST_OBJECTS)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -o $@ $^ $(LDFLAGS) $(LDLIBS)

PERF_RECORDER_TEST_OBJECTS = $(PERF_RECORDER_TEST_SOURCES:.cc=.o)
perf_recorder_test: LDLIBS += -lgtest -lelf -lcap
perf_recorder_test: %: $(COMMON_OBJECTS) $(TEST_COMMON_OBJECTS) \
		       $(PERF_RECORDER_TEST_OBJECTS)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -o $@ $^ $(LDFLAGS) $(LDLIBS)

QUIPPER_UNIT_TEST_OBJECTS = $(QUIPPER_UNIT_TEST_SOURCES:.cc=.o) \
	${CWP}/test_runner.o
unit_tests: LDLIBS += -lgtest -lelf -lcap
unit_tests: %: $(COMMON_OBJECTS) $(TEST_COMMON_OBJECTS) $(QUIPPER_UNIT_TEST_OBJECTS)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -o $@ $^ $(LDFLAGS) $(LDLIBS)

$(CONVERTER_UNIT_TEST_BINARIES): LDLIBS += -lgtest
$(CONVERTER_UNIT_TEST_BINARIES): %: %.o $(COMMON_OBJECTS) $(TEST_COMMON_OBJECTS)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -o $@ $^ $(LDFLAGS) $(LDLIBS)

# build all unit tests
tests: $(CONVERTER_UNIT_TEST_BINARIES)

# make run_(test binary name) runs the unit test.
CONVERTER_UNIT_TEST_RUN_BINARIES = $(CONVERTER_UNIT_TEST_BINARIES:%=run_%)
$(CONVERTER_UNIT_TEST_RUN_BINARIES): run_%: %
	./$^

# run all unit tests
check: $(CONVERTER_UNIT_TEST_RUN_BINARIES)

clean:
	rm -f *.d $(TESTS) $(GENERATED_SOURCES)  $(GENERATED_HEADERS) \
		$(TEST_COMMON_OBJECTS) $(TEST_OBJECTS) $(COMMON_OBJECTS) $(INTERMEDIATES) \
		$(MAIN_OBJECTS) $(PROGRAMS) $(TEST_BINARIES)

print-%:
	@echo $* = $($*)
