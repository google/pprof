// Copyright 2014 Google Inc. All Rights Reserved.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

package binutils

import (
	"bytes"
	"debug/elf"
	"encoding/binary"
	"errors"
	"fmt"
	"math"
	"path/filepath"
	"reflect"
	"regexp"
	"runtime"
	"strings"
	"testing"

	"github.com/google/pprof/internal/plugin"
)

var testAddrMap = map[int]string{
	1000: "_Z3fooid.clone2",
	2000: "_ZNSaIiEC1Ev.clone18",
	3000: "_ZNSt6vectorIS_IS_IiSaIiEESaIS1_EESaIS3_EEixEm",
}

func functionName(level int) (name string) {
	if name = testAddrMap[level]; name != "" {
		return name
	}
	return fmt.Sprintf("fun%d", level)
}

func TestAddr2Liner(t *testing.T) {
	const offset = 0x500

	a := addr2Liner{rw: &mockAddr2liner{}, base: offset}
	for i := 1; i < 8; i++ {
		addr := i*0x1000 + offset
		s, err := a.addrInfo(uint64(addr))
		if err != nil {
			t.Fatalf("addrInfo(%#x): %v", addr, err)
		}
		if len(s) != i {
			t.Fatalf("addrInfo(%#x): got len==%d, want %d", addr, len(s), i)
		}
		for l, f := range s {
			level := (len(s) - l) * 1000
			want := plugin.Frame{Func: functionName(level), File: fmt.Sprintf("file%d", level), Line: level}

			if f != want {
				t.Errorf("AddrInfo(%#x)[%d]: = %+v, want %+v", addr, l, f, want)
			}
		}
	}
	s, err := a.addrInfo(0xFFFF)
	if err != nil {
		t.Fatalf("addrInfo(0xFFFF): %v", err)
	}
	if len(s) != 0 {
		t.Fatalf("AddrInfo(0xFFFF): got len==%d, want 0", len(s))
	}
	a.rw.close()
}

type mockAddr2liner struct {
	output []string
}

func (a *mockAddr2liner) write(s string) error {
	var lines []string
	switch s {
	case "1000":
		lines = []string{"_Z3fooid.clone2", "file1000:1000"}
	case "2000":
		lines = []string{"_ZNSaIiEC1Ev.clone18", "file2000:2000", "_Z3fooid.clone2", "file1000:1000"}
	case "3000":
		lines = []string{"_ZNSt6vectorIS_IS_IiSaIiEESaIS1_EESaIS3_EEixEm", "file3000:3000", "_ZNSaIiEC1Ev.clone18", "file2000:2000", "_Z3fooid.clone2", "file1000:1000"}
	case "4000":
		lines = []string{"fun4000", "file4000:4000", "_ZNSt6vectorIS_IS_IiSaIiEESaIS1_EESaIS3_EEixEm", "file3000:3000", "_ZNSaIiEC1Ev.clone18", "file2000:2000", "_Z3fooid.clone2", "file1000:1000"}
	case "5000":
		lines = []string{"fun5000", "file5000:5000", "fun4000", "file4000:4000", "_ZNSt6vectorIS_IS_IiSaIiEESaIS1_EESaIS3_EEixEm", "file3000:3000", "_ZNSaIiEC1Ev.clone18", "file2000:2000", "_Z3fooid.clone2", "file1000:1000"}
	case "6000":
		lines = []string{"fun6000", "file6000:6000", "fun5000", "file5000:5000", "fun4000", "file4000:4000", "_ZNSt6vectorIS_IS_IiSaIiEESaIS1_EESaIS3_EEixEm", "file3000:3000", "_ZNSaIiEC1Ev.clone18", "file2000:2000", "_Z3fooid.clone2", "file1000:1000"}
	case "7000":
		lines = []string{"fun7000", "file7000:7000", "fun6000", "file6000:6000", "fun5000", "file5000:5000", "fun4000", "file4000:4000", "_ZNSt6vectorIS_IS_IiSaIiEESaIS1_EESaIS3_EEixEm", "file3000:3000", "_ZNSaIiEC1Ev.clone18", "file2000:2000", "_Z3fooid.clone2", "file1000:1000"}
	case "8000":
		lines = []string{"fun8000", "file8000:8000", "fun7000", "file7000:7000", "fun6000", "file6000:6000", "fun5000", "file5000:5000", "fun4000", "file4000:4000", "_ZNSt6vectorIS_IS_IiSaIiEESaIS1_EESaIS3_EEixEm", "file3000:3000", "_ZNSaIiEC1Ev.clone18", "file2000:2000", "_Z3fooid.clone2", "file1000:1000"}
	case "9000":
		lines = []string{"fun9000", "file9000:9000", "fun8000", "file8000:8000", "fun7000", "file7000:7000", "fun6000", "file6000:6000", "fun5000", "file5000:5000", "fun4000", "file4000:4000", "_ZNSt6vectorIS_IS_IiSaIiEESaIS1_EESaIS3_EEixEm", "file3000:3000", "_ZNSaIiEC1Ev.clone18", "file2000:2000", "_Z3fooid.clone2", "file1000:1000"}
	default:
		lines = []string{"??", "??:0"}
	}
	a.output = append(a.output, "0x"+s)
	a.output = append(a.output, lines...)
	return nil
}

func (a *mockAddr2liner) readLine() (string, error) {
	if len(a.output) == 0 {
		return "", fmt.Errorf("end of file")
	}
	next := a.output[0]
	a.output = a.output[1:]
	return next, nil
}

func (a *mockAddr2liner) close() {
}

func TestAddr2LinerLookup(t *testing.T) {
	for _, tc := range []struct {
		desc             string
		nmOutput         string
		wantSymbolized   map[uint64]string
		wantUnsymbolized []uint64
	}{
		{
			desc: "odd symbol count",
			nmOutput: `
0x1000 T 1000 100
0x2000 T 2000 120
0x3000 T 3000 130
`,
			wantSymbolized: map[uint64]string{
				0x1000: "0x1000",
				0x1001: "0x1000",
				0x1FFF: "0x1000",
				0x2000: "0x2000",
				0x2001: "0x2000",
				0x3000: "0x3000",
				0x312f: "0x3000",
			},
			wantUnsymbolized: []uint64{0x0fff, 0x3130},
		},
		{
			desc: "even symbol count",
			nmOutput: `
0x1000 T 1000 100
0x2000 T 2000 120
0x3000 T 3000 130
0x4000 T 4000 140
`,
			wantSymbolized: map[uint64]string{
				0x1000: "0x1000",
				0x1001: "0x1000",
				0x1FFF: "0x1000",
				0x2000: "0x2000",
				0x2fff: "0x2000",
				0x3000: "0x3000",
				0x3fff: "0x3000",
				0x4000: "0x4000",
				0x413f: "0x4000",
			},
			wantUnsymbolized: []uint64{0x0fff, 0x4140},
		},
		{
			desc: "different symbol types",
			nmOutput: `
absolute_0x100 a 100
absolute_0x200 A 200
text_0x1000 t 1000 100
bss_0x2000 b 2000 120
data_0x3000 d 3000 130
rodata_0x4000 r 4000 140
weak_0x5000 v 5000 150
text_0x6000 T 6000 160
bss_0x7000 B 7000 170
data_0x8000 D 8000 180
rodata_0x9000 R 9000 190
weak_0xa000 V a000 1a0
weak_0xb000 W b000 1b0
`,
			wantSymbolized: map[uint64]string{
				0x1000: "text_0x1000",
				0x1FFF: "text_0x1000",
				0x2000: "bss_0x2000",
				0x211f: "bss_0x2000",
				0x3000: "data_0x3000",
				0x312f: "data_0x3000",
				0x4000: "rodata_0x4000",
				0x413f: "rodata_0x4000",
				0x5000: "weak_0x5000",
				0x514f: "weak_0x5000",
				0x6000: "text_0x6000",
				0x6fff: "text_0x6000",
				0x7000: "bss_0x7000",
				0x716f: "bss_0x7000",
				0x8000: "data_0x8000",
				0x817f: "data_0x8000",
				0x9000: "rodata_0x9000",
				0x918f: "rodata_0x9000",
				0xa000: "weak_0xa000",
				0xa19f: "weak_0xa000",
				0xb000: "weak_0xb000",
				0xb1af: "weak_0xb000",
			},
			wantUnsymbolized: []uint64{0x100, 0x200, 0x0fff, 0x2120, 0x3130, 0x4140, 0x5150, 0x7170, 0x8180, 0x9190, 0xa1a0, 0xb1b0},
		},
	} {
		t.Run(tc.desc, func(t *testing.T) {
			a, err := parseAddr2LinerNM(0, bytes.NewBufferString(tc.nmOutput))
			if err != nil {
				t.Fatalf("nm parse error: %v", err)
			}
			for address, want := range tc.wantSymbolized {
				if got, _ := a.addrInfo(address); !checkAddress(got, address, want) {
					t.Errorf("%x: got %v, want %s", address, got, want)
				}
			}
			for _, unknown := range tc.wantUnsymbolized {
				if got, _ := a.addrInfo(unknown); got != nil {
					t.Errorf("%x: got %v, want nil", unknown, got)
				}
			}
		})
	}
}

func checkAddress(got []plugin.Frame, address uint64, want string) bool {
	if len(got) != 1 {
		return false
	}
	return got[0].Func == want
}

func TestSetTools(t *testing.T) {
	// Test that multiple calls work.
	bu := &Binutils{}
	bu.SetTools("")
	bu.SetTools("")
}

func TestSetFastSymbolization(t *testing.T) {
	// Test that multiple calls work.
	bu := &Binutils{}
	bu.SetFastSymbolization(true)
	bu.SetFastSymbolization(false)
}

func skipUnlessLinuxAmd64(t *testing.T) {
	if runtime.GOOS != "linux" || runtime.GOARCH != "amd64" {
		t.Skip("This test only works on x86-64 Linux")
	}
}

func skipUnlessDarwinAmd64(t *testing.T) {
	if runtime.GOOS != "darwin" || runtime.GOARCH != "amd64" {
		t.Skip("This test only works on x86-64 macOS")
	}
}

func skipUnlessWindowsAmd64(t *testing.T) {
	if runtime.GOOS != "windows" || runtime.GOARCH != "amd64" {
		t.Skip("This test only works on x86-64 Windows")
	}
}

func testDisasm(t *testing.T, intelSyntax bool) {
	_, llvmObjdump, buObjdump := findObjdump([]string{""})
	if !(llvmObjdump || buObjdump) {
		t.Skip("cannot disasm: no objdump tool available")
	}

	bu := &Binutils{}
	var testexe string
	switch runtime.GOOS {
	case "linux":
		testexe = "exe_linux_64"
	case "darwin":
		testexe = "exe_mac_64"
	case "windows":
		testexe = "exe_windows_64.exe"
	default:
		t.Skipf("unsupported OS %q", runtime.GOOS)
	}

	insts, err := bu.Disasm(filepath.Join("testdata", testexe), 0, math.MaxUint64, intelSyntax)
	if err != nil {
		t.Fatalf("Disasm: unexpected error %v", err)
	}
	mainCount := 0
	for _, x := range insts {
		// macOS symbols have a leading underscore.
		if x.Function == "main" || x.Function == "_main" {
			mainCount++
		}
	}
	if mainCount == 0 {
		t.Error("Disasm: found no main instructions")
	}
}

func TestDisasm(t *testing.T) {
	if (runtime.GOOS != "linux" && runtime.GOOS != "darwin" && runtime.GOOS != "windows") || runtime.GOARCH != "amd64" {
		t.Skip("This test only works on x86-64 Linux, macOS or Windows")
	}
	testDisasm(t, false)
}

func TestDisasmIntelSyntax(t *testing.T) {
	if (runtime.GOOS != "linux" && runtime.GOOS != "darwin" && runtime.GOOS != "windows") || runtime.GOARCH != "amd64" {
		t.Skip("This test only works on x86_64 Linux, macOS or Windows as it tests Intel asm syntax")
	}
	testDisasm(t, true)
}

func findSymbol(syms []*plugin.Sym, name string) *plugin.Sym {
	for _, s := range syms {
		for _, n := range s.Name {
			if n == name {
				return s
			}
		}
	}
	return nil
}

func TestObjFile(t *testing.T) {
	// If this test fails, check the address for main function in testdata/exe_linux_64
	// using the command 'nm -n '. Update the hardcoded addresses below to match
	// the addresses from the output.
	skipUnlessLinuxAmd64(t)
	for _, tc := range []struct {
		desc                 string
		start, limit, offset uint64
		addr                 uint64
	}{
		{"fixed load address", 0x400000, 0x4006fc, 0, 0x40052d},
		// True user-mode ASLR binaries are ET_DYN rather than ET_EXEC so this case
		// is a bit artificial except that it approximates the
		// vmlinux-with-kernel-ASLR case where the binary *is* ET_EXEC.
		{"simulated ASLR address", 0x500000, 0x5006fc, 0, 0x50052d},
	} {
		t.Run(tc.desc, func(t *testing.T) {
			bu := &Binutils{}
			f, err := bu.Open(filepath.Join("testdata", "exe_linux_64"), tc.start, tc.limit, tc.offset, "")
			if err != nil {
				t.Fatalf("Open: unexpected error %v", err)
			}
			defer f.Close()
			syms, err := f.Symbols(regexp.MustCompile("main"), 0)
			if err != nil {
				t.Fatalf("Symbols: unexpected error %v", err)
			}

			m := findSymbol(syms, "main")
			if m == nil {
				t.Fatalf("Symbols: did not find main")
			}
			addr, err := f.ObjAddr(tc.addr)
			if err != nil {
				t.Fatalf("ObjAddr(%x) failed: %v", tc.addr, err)
			}
			if addr != m.Start {
				t.Errorf("ObjAddr(%x) got %x, want %x", tc.addr, addr, m.Start)
			}
			gotFrames, err := f.SourceLine(tc.addr)
			if err != nil {
				t.Fatalf("SourceLine: unexpected error %v", err)
			}
			wantFrames := []plugin.Frame{
				{Func: "main", File: "/tmp/hello.c", Line: 3},
			}
			if !reflect.DeepEqual(gotFrames, wantFrames) {
				t.Fatalf("SourceLine for main: got %v; want %v\n", gotFrames, wantFrames)
			}
		})
	}
}

func TestMachoFiles(t *testing.T) {
	// If this test fails, check the address for main function in testdata/exe_mac_64
	// and testdata/lib_mac_64 using addr2line or gaddr2line. Update the
	// hardcoded addresses below to match the addresses from the output.
	skipUnlessDarwinAmd64(t)

	// Load `file`, pretending it was mapped at `start`. Then get the symbol
	// table. Check that it contains the symbol `sym` and that the address
	// `addr` gives the `expected` stack trace.
	for _, tc := range []struct {
		desc                 string
		file                 string
		start, limit, offset uint64
		addr                 uint64
		sym                  string
		expected             []plugin.Frame
	}{
		{"normal mapping", "exe_mac_64", 0x100000000, math.MaxUint64, 0,
			0x100000f50, "_main",
			[]plugin.Frame{
				{Func: "main", File: "/tmp/hello.c", Line: 3},
			}},
		{"other mapping", "exe_mac_64", 0x200000000, math.MaxUint64, 0,
			0x200000f50, "_main",
			[]plugin.Frame{
				{Func: "main", File: "/tmp/hello.c", Line: 3},
			}},
		{"lib normal mapping", "lib_mac_64", 0, math.MaxUint64, 0,
			0xfa0, "_bar",
			[]plugin.Frame{
				{Func: "bar", File: "/tmp/lib.c", Line: 5},
			}},
	} {
		t.Run(tc.desc, func(t *testing.T) {
			bu := &Binutils{}
			f, err := bu.Open(filepath.Join("testdata", tc.file), tc.start, tc.limit, tc.offset, "")
			if err != nil {
				t.Fatalf("Open: unexpected error %v", err)
			}
			t.Logf("binutils: %v", bu)
			if runtime.GOOS == "darwin" && !bu.rep.addr2lineFound && !bu.rep.llvmSymbolizerFound {
				// On macOS, user needs to install gaddr2line or llvm-symbolizer with
				// Homebrew, skip the test when the environment doesn't have it
				// installed.
				t.Skip("couldn't find addr2line or gaddr2line")
			}
			defer f.Close()
			syms, err := f.Symbols(nil, 0)
			if err != nil {
				t.Fatalf("Symbols: unexpected error %v", err)
			}

			m := findSymbol(syms, tc.sym)
			if m == nil {
				t.Fatalf("Symbols: could not find symbol %v", tc.sym)
			}
			gotFrames, err := f.SourceLine(tc.addr)
			if err != nil {
				t.Fatalf("SourceLine: unexpected error %v", err)
			}
			if !reflect.DeepEqual(gotFrames, tc.expected) {
				t.Fatalf("SourceLine for main: got %v; want %v\n", gotFrames, tc.expected)
			}
		})
	}
}

func TestLLVMSymbolizer(t *testing.T) {
	if runtime.GOOS != "linux" {
		t.Skip("testtdata/llvm-symbolizer has only been tested on linux")
	}

	cmd := filepath.Join("testdata", "fake-llvm-symbolizer")
	for _, c := range []struct {
		addr   uint64
		isData bool
		frames []plugin.Frame
	}{
		{0x10, false, []plugin.Frame{
			{Func: "Inlined_0x10", File: "foo.h", Line: 0, Column: 0},
			{Func: "Func_0x10", File: "foo.c", Line: 2, Column: 1},
		}},
		{0x20, true, []plugin.Frame{
			{Func: "foo_0x20", File: "0x20 8"},
		}},
	} {
		desc := fmt.Sprintf("Code %x", c.addr)
		if c.isData {
			desc = fmt.Sprintf("Data %x", c.addr)
		}
		t.Run(desc, func(t *testing.T) {
			symbolizer, err := newLLVMSymbolizer(cmd, "foo", 0, c.isData)
			if err != nil {
				t.Fatalf("newLLVMSymbolizer: unexpected error %v", err)
			}
			defer symbolizer.rw.close()

			frames, err := symbolizer.addrInfo(c.addr)
			if err != nil {
				t.Fatalf("LLVM: unexpected error %v", err)
			}
			if !reflect.DeepEqual(frames, c.frames) {
				t.Errorf("LLVM: expect %v; got %v\n", c.frames, frames)
			}
		})
	}
}

func TestPEFile(t *testing.T) {
	// If this test fails, check the address for main function in testdata/exe_windows_64.exe
	// using the command 'nm -n '. Update the hardcoded addresses below to match
	// the addresses from the output.
	skipUnlessWindowsAmd64(t)
	for _, tc := range []struct {
		desc                 string
		start, limit, offset uint64
		addr                 uint64
	}{
		{"fake mapping", 0, math.MaxUint64, 0, 0x140001594},
		{"fixed load address", 0x140000000, 0x140002000, 0, 0x140001594},
		{"simulated ASLR address", 0x150000000, 0x150002000, 0, 0x150001594},
	} {
		t.Run(tc.desc, func(t *testing.T) {
			bu := &Binutils{}
			f, err := bu.Open(filepath.Join("testdata", "exe_windows_64.exe"), tc.start, tc.limit, tc.offset, "")
			if err != nil {
				t.Fatalf("Open: unexpected error %v", err)
			}
			defer f.Close()
			syms, err := f.Symbols(regexp.MustCompile("main"), 0)
			if err != nil {
				t.Fatalf("Symbols: unexpected error %v", err)
			}

			m := findSymbol(syms, "main")
			if m == nil {
				t.Fatalf("Symbols: did not find main")
			}
			addr, err := f.ObjAddr(tc.addr)
			if err != nil {
				t.Fatalf("ObjAddr(%x) failed: %v", tc.addr, err)
			}
			if addr != m.Start {
				t.Errorf("ObjAddr(%x) got %x, want %x", tc.addr, addr, m.Start)
			}
			gotFrames, err := f.SourceLine(tc.addr)
			if err != nil {
				t.Fatalf("SourceLine: unexpected error %v", err)
			}
			wantFrames := []plugin.Frame{
				{Func: "main", File: "hello.c", Line: 3, Column: 12},
			}
			if !reflect.DeepEqual(gotFrames, wantFrames) {
				t.Fatalf("SourceLine for main: got %v; want %v\n", gotFrames, wantFrames)
			}
		})
	}
}

func TestOpenMalformedELF(t *testing.T) {
	// Test that opening a malformed ELF file will report an error containing
	// the word "ELF".
	bu := &Binutils{}
	_, err := bu.Open(filepath.Join("testdata", "malformed_elf"), 0, 0, 0, "")
	if err == nil {
		t.Fatalf("Open: unexpected success")
	}

	if !strings.Contains(err.Error(), "ELF") {
		t.Errorf("Open: got %v, want error containing 'ELF'", err)
	}
}

func TestOpenMalformedMachO(t *testing.T) {
	// Test that opening a malformed Mach-O file will report an error containing
	// the word "Mach-O".
	bu := &Binutils{}
	_, err := bu.Open(filepath.Join("testdata", "malformed_macho"), 0, 0, 0, "")
	if err == nil {
		t.Fatalf("Open: unexpected success")
	}

	if !strings.Contains(err.Error(), "Mach-O") {
		t.Errorf("Open: got %v, want error containing 'Mach-O'", err)
	}
}

func TestObjdumpVersionChecks(t *testing.T) {
	// Test that the objdump version strings are parsed properly.
	type testcase struct {
		desc string
		os   string
		ver  string
		want bool
	}

	for _, tc := range []testcase{
		{
			desc: "Valid Apple LLVM version string with usable version",
			os:   "darwin",
			ver:  "Apple LLVM version 11.0.3 (clang-1103.0.32.62)\nOptimized build.",
			want: true,
		},
		{
			desc: "Valid Apple LLVM version string with unusable version",
			os:   "darwin",
			ver:  "Apple LLVM version 10.0.0 (clang-1000.11.45.5)\nOptimized build.",
			want: false,
		},
		{
			desc: "Invalid Apple LLVM version string with usable version",
			os:   "darwin",
			ver:  "Apple LLVM versions 11.0.3 (clang-1103.0.32.62)\nOptimized build.",
			want: false,
		},
		{
			desc: "Valid LLVM version string with usable version",
			os:   "linux",
			ver:  "LLVM (http://llvm.org/):\nLLVM version 9.0.1\n\nOptimized build.",
			want: true,
		},
		{
			desc: "Valid LLVM version string with unusable version",
			os:   "linux",
			ver:  "LLVM (http://llvm.org/):\nLLVM version 6.0.1\n\nOptimized build.",
			want: false,
		},
		{
			desc: "Invalid LLVM version string with usable version",
			os:   "linux",
			ver:  "LLVM (http://llvm.org/):\nLLVM versions 9.0.1\n\nOptimized build.",
			want: false,
		},
		{
			desc: "Valid LLVM objdump version string with trunk",
			os:   runtime.GOOS,
			ver:  "LLVM (http://llvm.org/):\nLLVM version custom-trunk 124ffeb592a00bfe\nOptimized build.",
			want: true,
		},
		{
			desc: "Invalid LLVM objdump version string with trunk",
			os:   runtime.GOOS,
			ver:  "LLVM (http://llvm.org/):\nLLVM version custom-trank 124ffeb592a00bfe\nOptimized build.",
			want: false,
		},
		{
			desc: "Invalid LLVM objdump version string with trunk",
			os:   runtime.GOOS,
			ver:  "LLVM (http://llvm.org/):\nllvm version custom-trunk 124ffeb592a00bfe\nOptimized build.",
			want: false,
		},
	} {
		if runtime.GOOS == tc.os {
			if got := isLLVMObjdump(tc.ver); got != tc.want {
				t.Errorf("%v: got %v, want %v", tc.desc, got, tc.want)
			}
		}
	}
	for _, tc := range []testcase{
		{
			desc: "Valid GNU objdump version string",
			ver:  "GNU objdump (GNU Binutils) 2.34\nCopyright (C) 2020 Free Software Foundation, Inc.",
			want: true,
		},
		{
			desc: "Invalid GNU objdump version string",
			ver:  "GNU nm (GNU Binutils) 2.34\nCopyright (C) 2020 Free Software Foundation, Inc.",
			want: false,
		},
	} {
		if got := isBuObjdump(tc.ver); got != tc.want {
			t.Errorf("%v: got %v, want %v", tc.desc, got, tc.want)
		}
	}
}

func TestComputeBase(t *testing.T) {
	realELFOpen := elfOpen
	defer func() {
		elfOpen = realELFOpen
	}()

	tinyExecFile := &elf.File{
		FileHeader: elf.FileHeader{Type: elf.ET_EXEC},
		Progs: []*elf.Prog{
			{ProgHeader: elf.ProgHeader{Type: elf.PT_PHDR, Flags: elf.PF_R | elf.PF_X, Off: 0x40, Vaddr: 0x400040, Paddr: 0x400040, Filesz: 0x1f8, Memsz: 0x1f8, Align: 8}},
			{ProgHeader: elf.ProgHeader{Type: elf.PT_INTERP, Flags: elf.PF_R, Off: 0x238, Vaddr: 0x400238, Paddr: 0x400238, Filesz: 0x1c, Memsz: 0x1c, Align: 1}},
			{ProgHeader: elf.ProgHeader{Type: elf.PT_LOAD, Flags: elf.PF_R | elf.PF_X, Off: 0, Vaddr: 0, Paddr: 0, Filesz: 0xc80, Memsz: 0xc80, Align: 0x200000}},
			{ProgHeader: elf.ProgHeader{Type: elf.PT_LOAD, Flags: elf.PF_R | elf.PF_W, Off: 0xc80, Vaddr: 0x200c80, Paddr: 0x200c80, Filesz: 0x1f0, Memsz: 0x1f0, Align: 0x200000}},
		},
	}
	tinyBadBSSExecFile := &elf.File{
		FileHeader: elf.FileHeader{Type: elf.ET_EXEC},
		Progs: []*elf.Prog{
			{ProgHeader: elf.ProgHeader{Type: elf.PT_PHDR, Flags: elf.PF_R | elf.PF_X, Off: 0x40, Vaddr: 0x400040, Paddr: 0x400040, Filesz: 0x1f8, Memsz: 0x1f8, Align: 8}},
			{ProgHeader: elf.ProgHeader{Type: elf.PT_INTERP, Flags: elf.PF_R, Off: 0x238, Vaddr: 0x400238, Paddr: 0x400238, Filesz: 0x1c, Memsz: 0x1c, Align: 1}},
			{ProgHeader: elf.ProgHeader{Type: elf.PT_LOAD, Flags: elf.PF_R | elf.PF_X, Off: 0, Vaddr: 0, Paddr: 0, Filesz: 0xc80, Memsz: 0xc80, Align: 0x200000}},
			{ProgHeader: elf.ProgHeader{Type: elf.PT_LOAD, Flags: elf.PF_R | elf.PF_W, Off: 0xc80, Vaddr: 0x200c80, Paddr: 0x200c80, Filesz: 0x100, Memsz: 0x1f0, Align: 0x200000}},
			{ProgHeader: elf.ProgHeader{Type: elf.PT_LOAD, Flags: elf.PF_R | elf.PF_W, Off: 0xd80, Vaddr: 0x400d80, Paddr: 0x400d80, Filesz: 0x90, Memsz: 0x90, Align: 0x200000}},
		},
	}

	for _, tc := range []struct {
		desc       string
		file       *elf.File
		openErr    error
		mapping    *elfMapping
		addr       uint64
		wantError  bool
		wantBase   uint64
		wantIsData bool
	}{
		{
			desc:       "no elf mapping, no error",
			mapping:    nil,
			addr:       0x1000,
			wantBase:   0,
			wantIsData: false,
		},
		{
			desc:      "address outside mapping bounds means error",
			file:      &elf.File{},
			mapping:   &elfMapping{start: 0x2000, limit: 0x5000, offset: 0x1000},
			addr:      0x1000,
			wantError: true,
		},
		{
			desc:      "elf.Open failing means error",
			file:      &elf.File{FileHeader: elf.FileHeader{Type: elf.ET_EXEC}},
			openErr:   errors.New("elf.Open failed"),
			mapping:   &elfMapping{start: 0x2000, limit: 0x5000, offset: 0x1000},
			addr:      0x4000,
			wantError: true,
		},
		{
			desc:       "no loadable segments, no error",
			file:       &elf.File{FileHeader: elf.FileHeader{Type: elf.ET_EXEC}},
			mapping:    &elfMapping{start: 0x2000, limit: 0x5000, offset: 0x1000},
			addr:       0x4000,
			wantBase:   0,
			wantIsData: false,
		},
		{
			desc:      "unsupported executable type, Get Base returns error",
			file:      &elf.File{FileHeader: elf.FileHeader{Type: elf.ET_NONE}},
			mapping:   &elfMapping{start: 0x2000, limit: 0x5000, offset: 0x1000},
			addr:      0x4000,
			wantError: true,
		},
		{
			desc:       "tiny file select executable segment by offset",
			file:       tinyExecFile,
			mapping:    &elfMapping{start: 0x5000000, limit: 0x5001000, offset: 0x0},
			addr:       0x5000c00,
			wantBase:   0x5000000,
			wantIsData: false,
		},
		{
			desc:       "tiny file select data segment by offset",
			file:       tinyExecFile,
			mapping:    &elfMapping{start: 0x5200000, limit: 0x5201000, offset: 0x0},
			addr:       0x5200c80,
			wantBase:   0x5000000,
			wantIsData: true,
		},
		{
			desc:      "tiny file offset outside any segment means error",
			file:      tinyExecFile,
			mapping:   &elfMapping{start: 0x5200000, limit: 0x5201000, offset: 0x0},
			addr:      0x5200e70,
			wantError: true,
		},
		{
			desc:       "tiny file with bad BSS segment selects data segment by offset in initialized section",
			file:       tinyBadBSSExecFile,
			mapping:    &elfMapping{start: 0x5200000, limit: 0x5201000, offset: 0x0},
			addr:       0x5200d79,
			wantBase:   0x5000000,
			wantIsData: true,
		},
		{
			desc:      "tiny file with bad BSS segment with offset in uninitialized section means error",
			file:      tinyBadBSSExecFile,
			mapping:   &elfMapping{start: 0x5200000, limit: 0x5201000, offset: 0x0},
			addr:      0x5200d80,
			wantError: true,
		},
	} {
		t.Run(tc.desc, func(t *testing.T) {
			elfOpen = func(_ string) (*elf.File, error) {
				return tc.file, tc.openErr
			}
			f := file{m: tc.mapping}
			err := f.computeBase(tc.addr)
			if (err != nil) != tc.wantError {
				t.Errorf("got error %v, want any error=%v", err, tc.wantError)
			}
			if err != nil {
				return
			}
			if f.base != tc.wantBase {
				t.Errorf("got base %x, want %x", f.base, tc.wantBase)
			}
			if f.isData != tc.wantIsData {
				t.Errorf("got isData %v, want %v", f.isData, tc.wantIsData)
			}
		})
	}
}

func TestELFObjAddr(t *testing.T) {
	// The exe_linux_64 has two loadable program headers:
	//  LOAD           0x0000000000000000 0x0000000000400000 0x0000000000400000
	//                 0x00000000000006fc 0x00000000000006fc  R E    0x200000
	//  LOAD           0x0000000000000e10 0x0000000000600e10 0x0000000000600e10
	//                 0x0000000000000230 0x0000000000000238  RW     0x200000
	name := filepath.Join("testdata", "exe_linux_64")

	for _, tc := range []struct {
		desc                 string
		start, limit, offset uint64
		wantOpenError        bool
		addr                 uint64
		wantObjAddr          uint64
		wantAddrError        bool
	}{
		{"exec mapping, good address", 0x5400000, 0x5401000, 0, false, 0x5400400, 0x400400, false},
		{"exec mapping, address outside segment", 0x5400000, 0x5401000, 0, false, 0x5400800, 0, true},
		{"short data mapping, good address", 0x5600e00, 0x5602000, 0xe00, false, 0x5600e10, 0x600e10, false},
		{"short data mapping, address outside segment", 0x5600e00, 0x5602000, 0xe00, false, 0x5600e00, 0x600e00, false},
		{"page aligned data mapping, good address", 0x5600000, 0x5602000, 0, false, 0x5601000, 0x601000, false},
		{"page aligned data mapping, address outside segment", 0x5600000, 0x5602000, 0, false, 0x5601048, 0, true},
		{"bad file offset, no matching segment", 0x5600000, 0x5602000, 0x2000, false, 0x5600e10, 0, true},
		{"large mapping size, match by sample offset", 0x5600000, 0x5603000, 0, false, 0x5600e10, 0x600e10, false},
	} {
		t.Run(tc.desc, func(t *testing.T) {
			b := binrep{}
			o, err := b.openELF(name, tc.start, tc.limit, tc.offset, "")
			if (err != nil) != tc.wantOpenError {
				t.Errorf("openELF got error %v, want any error=%v", err, tc.wantOpenError)
			}
			if err != nil {
				return
			}
			got, err := o.ObjAddr(tc.addr)
			if (err != nil) != tc.wantAddrError {
				t.Errorf("ObjAddr got error %v, want any error=%v", err, tc.wantAddrError)
			}
			if err != nil {
				return
			}
			if got != tc.wantObjAddr {
				t.Errorf("got ObjAddr %x; want %x\n", got, tc.wantObjAddr)
			}
		})
	}
}

type buf struct {
	data []byte
}

// write appends a null-terminated string and returns its starting index.
func (b *buf) write(s string) uint32 {
	res := uint32(len(b.data))
	b.data = append(b.data, s...)
	b.data = append(b.data, '\x00')
	return res
}

// fakeELFFile generates a minimal valid ELF file, with fake .head.text and
// .text sections, and their corresponding _text and _stext start symbols,
// mimicking a kernel vmlinux image.
func fakeELFFile(t *testing.T) *elf.File {
	var (
		sizeHeader64  = binary.Size(elf.Header64{})
		sizeProg64    = binary.Size(elf.Prog64{})
		sizeSection64 = binary.Size(elf.Section64{})
	)

	const (
		textAddr  = 0xffff000010080000
		stextAddr = 0xffff000010081000
	)

	// Generate magic to identify as an ELF file.
	var ident [16]uint8
	ident[0] = '\x7f'
	ident[1] = 'E'
	ident[2] = 'L'
	ident[3] = 'F'
	ident[elf.EI_CLASS] = uint8(elf.ELFCLASS64)
	ident[elf.EI_DATA] = uint8(elf.ELFDATA2LSB)
	ident[elf.EI_VERSION] = uint8(elf.EV_CURRENT)
	ident[elf.EI_OSABI] = uint8(elf.ELFOSABI_NONE)

	// A single program header, containing code and starting at the _text address.
	progs := []elf.Prog64{{
		Type: uint32(elf.PT_LOAD), Flags: uint32(elf.PF_R | elf.PF_X), Off: 0x10000, Vaddr: textAddr, Paddr: textAddr, Filesz: 0x1234567, Memsz: 0x1234567, Align: 0x10000}}

	symNames := buf{}
	syms := []elf.Sym64{
		{}, // first symbol empty by convention
		{Name: symNames.write("_text"), Info: 0, Other: 0, Shndx: 0, Value: textAddr, Size: 0},
		{Name: symNames.write("_stext"), Info: 0, Other: 0, Shndx: 0, Value: stextAddr, Size: 0},
	}

	const numSections = 5
	// We'll write `textSize` zero bytes as contents of the .head.text and .text sections.
	const textSize = 16
	// Offset of section contents in the byte stream -- after header, program headers, and section headers.
	sectionsStart := uint64(sizeHeader64 + len(progs)*sizeProg64 + numSections*sizeSection64)

	secNames := buf{}
	sections := [numSections]elf.Section64{
		{Name: secNames.write(".head.text"), Type: uint32(elf.SHT_PROGBITS), Flags: uint64(elf.SHF_ALLOC | elf.SHF_EXECINSTR), Addr: textAddr, Off: sectionsStart, Size: textSize, Link: 0, Info: 0, Addralign: 2048, Entsize: 0},
		{Name: secNames.write(".text"), Type: uint32(elf.SHT_PROGBITS), Flags: uint64(elf.SHF_ALLOC | elf.SHF_EXECINSTR), Addr: stextAddr, Off: sectionsStart + textSize, Size: textSize, Link: 0, Info: 0, Addralign: 2048, Entsize: 0},
		{Name: secNames.write(".symtab"), Type: uint32(elf.SHT_SYMTAB), Flags: 0, Addr: 0, Off: sectionsStart + 2*textSize, Size: uint64(len(syms) * elf.Sym64Size), Link: 3 /*index of .strtab*/, Info: 0, Addralign: 8, Entsize: elf.Sym64Size},
		{Name: secNames.write(".strtab"), Type: uint32(elf.SHT_STRTAB), Flags: 0, Addr: 0, Off: sectionsStart + 2*textSize + uint64(len(syms)*elf.Sym64Size), Size: uint64(len(symNames.data)), Link: 0, Info: 0, Addralign: 1, Entsize: 0},
		{Name: secNames.write(".shstrtab"), Type: uint32(elf.SHT_STRTAB), Flags: 0, Addr: 0, Off: sectionsStart + 2*textSize + uint64(len(syms)*elf.Sym64Size+len(symNames.data)), Size: uint64(len(secNames.data)), Link: 0, Info: 0, Addralign: 1, Entsize: 0},
	}

	hdr := elf.Header64{
		Ident:     ident,
		Type:      uint16(elf.ET_DYN),
		Machine:   uint16(elf.EM_AARCH64),
		Version:   uint32(elf.EV_CURRENT),
		Entry:     textAddr,
		Phoff:     uint64(sizeHeader64),
		Shoff:     uint64(sizeHeader64 + len(progs)*sizeProg64),
		Flags:     0,
		Ehsize:    uint16(sizeHeader64),
		Phentsize: uint16(sizeProg64),
		Phnum:     uint16(len(progs)),
		Shentsize: uint16(sizeSection64),
		Shnum:     uint16(len(sections)),
		Shstrndx:  4, // index of .shstrtab
	}

	// Serialize all headers and sections into a single binary stream.
	var data bytes.Buffer
	for i, b := range []interface{}{hdr, progs, sections, [textSize]byte{}, [textSize]byte{}, syms, symNames.data, secNames.data} {
		err := binary.Write(&data, binary.LittleEndian, b)
		if err != nil {
			t.Fatalf("Write(%v) got err %v, want nil", i, err)
		}
	}

	// ... and parse it as and ELF file.
	ef, err := elf.NewFile(bytes.NewReader(data.Bytes()))
	if err != nil {
		t.Fatalf("elf.NewFile got err %v, want nil", err)
	}
	return ef
}

func TestELFKernelOffset(t *testing.T) {
	realELFOpen := elfOpen
	defer func() {
		elfOpen = realELFOpen
	}()

	wantAddr := uint64(0xffff000010082000)
	elfOpen = func(_ string) (*elf.File, error) {
		return fakeELFFile(t), nil
	}

	for _, tc := range []struct {
		name             string
		relocationSymbol string
		start            uint64
	}{
		{"text", "_text", 0xffff000020080000},
		{"stext", "_stext", 0xffff000020081000},
	} {

		b := binrep{}
		o, err := b.openELF("vmlinux", tc.start, 0xffffffffffffffff, tc.start, tc.relocationSymbol)
		if err != nil {
			t.Errorf("%v: openELF got error %v, want nil", tc.name, err)
			continue
		}

		addr, err := o.ObjAddr(0xffff000020082000)
		if err != nil {
			t.Errorf("%v: ObjAddr got err %v, want nil", tc.name, err)
			continue
		}
		if addr != wantAddr {
			t.Errorf("%v: ObjAddr got %x, want %x", tc.name, addr, wantAddr)
		}

	}
}
