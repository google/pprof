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
PC_DEPS = openssl
PC_CFLAGS := $(shell $(PKG_CONFIG) --cflags $(PC_DEPS))
PC_LIBS := $(shell $(PKG_CONFIG) --libs $(PC_DEPS))

CWP = quipper

CXXFLAGS += -std=c++11 -g -Wall -Werror -Wall -Wno-error
CPPFLAGS += -Icompat -I${CWP}/mybase \
		-I${CWP}/compat/ext \
		-I${CWP} \
		-Ithird_party \
		-I. $(PC_CFLAGS) $(PROTOBUF_CFLAGS) $(GTEST_INCLUDES)
LDLIBS += -lelf -lpthread $(PC_LIBS) $(PROTOBUF_LIBS)

PROGRAMS = perf_to_profile
MAIN_SOURCES = $(PROGRAMS:%=%.cc)
MAIN_OBJECTS = $(MAIN_SOURCES:%.cc=%.o)

QUIPPER_LIBRARY_SOURCES = \
	address_mapper.cc binary_data_utils.cc buffer_reader.cc buffer_writer.cc \
	conversion_utils.cc compat/log_level.cc data_reader.cc \
	data_writer.cc dso.cc file_reader.cc file_utils.cc \
	mybase/base/logging.cc perf_option_parser.cc perf_data_utils.cc \
	perf_parser.cc perf_protobuf_io.cc perf_reader.cc perf_recorder.cc \
	perf_serializer.cc perf_stat_parser.cc run_command.cc \
	sample_info_reader.cc scoped_temp_path.cc string_utils.cc \
  huge_page_deducer.cc
QUIPPER_LIBRARY_SOURCES := \
	$(QUIPPER_LIBRARY_SOURCES:%=${CWP}/%)
CONVERTER_LIBRARY_SOURCES = perf_data_converter.cc perf_data_handler.cc \
														builder.cc
LIBRARY_SOURCES = $(QUIPPER_LIBRARY_SOURCES) $(CONVERTER_LIBRARY_SOURCES)

QUIPPER_PROTOS = perf_data.proto perf_stat.proto
QUIPPER_PROTOS := $(QUIPPER_PROTOS:%=${CWP}/%)
CONVERTER_PROTOS = profile.proto
QUIPPER_GENERATED_SOURCES = $(QUIPPER_PROTOS:.proto=.pb.cc)
QUIPPER_GENERATED_HEADERS = $(QUIPPER_PROTOS:.proto=.pb.h)
GENERATED_SOURCES = $(CONVERTER_PROTOS:.proto=.pb.cc) \
	$(QUIPPER_GENERATED_SOURCES)
GENERATED_HEADERS = $(CONVERTER_PROTOS:.proto=.pb.h) \
	$(QUIPPER_GENERATED_HEADERS)

COMMON_SOURCES = $(GENERATED_SOURCES) $(LIBRARY_SOURCES)
COMMON_OBJECTS = $(COMMON_SOURCES:.cc=.o)

TEST_SOURCES = intervalmap_test.cc perf_data_converter_test.cc \
	perf_data_handler_test.cc
TEST_BINARIES = $(TEST_SOURCES:.cc=)
TEST_OBJECTS = $(TEST_SOURCES:.cc=.o)

ALL_SOURCES = $(MAIN_SOURCES) $(COMMON_SOURCES) $(TEST_SOURCES)

INTERMEDIATES = $(ALL_SOURCES:.cc=.d*)

all: $(PROGRAMS)
	@echo Sources compiled!

# Protobuf dependence configuration
ifeq ($(wildcard third_party/protobuf/src/google/protobuf/descriptor.pb.h),)
# Protobuf module hasn't been populated, attempt using local installation.
PROTOC = protoc
PROTOBUF_DEP =
PROTOBUF_CFLAGS := $(shell $(PKG_CONFIG) --cflags protobuf)
PROTOBUF_LIBS := $(shell $(PKG_CONFIG) --libs protobuf)
else
# Use protobuf compiler and libraries from submodule.
PROTOC = third_party/protobuf/src/protoc
PROTOBUF_CFLAGS := -Ithird_party/protobuf/src
PROTOBUF_LIBS := third_party/protobuf/src/.libs/libprotobuf.a  -lz
PROTOBUF_DEP := third_party/protobuf/src/.libs/libprotobuf.a
endif

third_party/protobuf/configure:
	echo "[AUTOGEN] Preparing protobuf"
	(cd third_party/protobuf ; autoreconf -f -i -Wall,no-obsolete)

third_party/protobuf/src/.libs/libprotobuf.a: third_party/protobuf/configure
	echo "[MAKE]    Building protobuf"
	(cd third_party/protobuf ; CC="$(CC)" CXX="$(CXX)" LDFLAGS="$(LDFLAGS_$(CONFIG)) -g $(PROTOBUF_LDFLAGS_EXTRA)" CPPFLAGS="$(PIC_CPPFLAGS) $(CPPFLAGS_$(CONFIG)) -g $(PROTOBUF_CPPFLAGS_EXTRA)" ./configure --disable-shared --enable-static $(PROTOBUF_CONFIG_OPTS))
	$(MAKE) -C third_party/protobuf clean
	$(MAKE) -C third_party/protobuf

# Googletest dependence configuration
ifeq ($(wildcard third_party/googletest/googletest/include/gtest/gtest.h),)
# Use local gtest includes, already on the system path
GTEST_INCLUDES =
GTEST_LIBS = -lgtest -lgmock
else
# Pick up gtest includes from submodule.
GTEST_INCLUDES = -Ithird_party/googletest/googlemock/include -Ithird_party/googletest/googletest/include
GTEST_LIBS = -Ithird_party/googletest/googlemock third_party/googletest/googlemock/src/gmock-all.cc -Ithird_party/googletest/googletest third_party/googletest/googletest/src/gtest-all.cc
endif

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
%.pb.h %.pb.cc: %.proto $(PROTOBUF_DEP)
	$(PROTOC) --cpp_out=. $<

# Do not remove protobuf headers that were generated as dependencies of other
# modules.
.SECONDARY: $(GENERATED_HEADERS)

$(PROGRAMS): %: %.o $(COMMON_OBJECTS)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -o $@ $^ $(LDFLAGS) $(LDLIBS)

$(TEST_BINARIES): %: %.o $(COMMON_OBJECTS)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -o $@ $^ $(LDFLAGS) $(LDLIBS) $(GTEST_LIBS)

# build all unit tests
tests: $(TEST_BINARIES)

# make run_(test binary name) runs the unit test.
UNIT_TEST_RUN_BINARIES = $(TEST_BINARIES:%=run_%)
$(UNIT_TEST_RUN_BINARIES): run_%: %
	./$^

# run all unit tests
check: $(UNIT_TEST_RUN_BINARIES)

clean:
	rm -f *.d $(TESTS) $(GENERATED_SOURCES)  $(GENERATED_HEADERS) \
		$(TEST_OBJECTS) $(COMMON_OBJECTS) $(INTERMEDIATES) \
		$(MAIN_OBJECTS) $(PROGRAMS) $(TEST_BINARIES)

print-%:
	@echo $* = $($*)
