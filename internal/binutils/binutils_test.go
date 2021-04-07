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
			f, err := bu.Open(filepath.Join("testdata", "exe_linux_64"), tc.start, tc.limit, tc.offset)
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
			for _, addr := range []uint64{m.Start + f.Base(), tc.addr} {
				gotFrames, err := f.SourceLine(addr)
				if err != nil {
					t.Fatalf("SourceLine: unexpected error %v", err)
				}
				wantFrames := []plugin.Frame{
					{Func: "main", File: "/tmp/hello.c", Line: 3},
				}
				if !reflect.DeepEqual(gotFrames, wantFrames) {
					t.Fatalf("SourceLine for main: got %v; want %v\n", gotFrames, wantFrames)
				}
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
			f, err := bu.Open(filepath.Join("testdata", tc.file), tc.start, tc.limit, tc.offset)
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
			{Func: "Inlined_0x10", File: "foo.h", Line: 0},
			{Func: "Func_0x10", File: "foo.c", Line: 2},
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
			f, err := bu.Open(filepath.Join("testdata", "exe_windows_64.exe"), tc.start, tc.limit, tc.offset)
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

			for _, addr := range []uint64{m.Start + f.Base(), tc.addr} {
				gotFrames, err := f.SourceLine(addr)
				if err != nil {
					t.Fatalf("SourceLine: unexpected error %v", err)
				}
				wantFrames := []plugin.Frame{
					{Func: "main", File: "hello.c", Line: 3},
				}
				if !reflect.DeepEqual(gotFrames, wantFrames) {
					t.Fatalf("SourceLine for main: got %v; want %v\n", gotFrames, wantFrames)
				}
			}
		})
	}
}

func TestOpenMalformedELF(t *testing.T) {
	// Test that opening a malformed ELF file will report an error containing
	// the word "ELF".
	bu := &Binutils{}
	_, err := bu.Open(filepath.Join("testdata", "malformed_elf"), 0, 0, 0)
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
	_, err := bu.Open(filepath.Join("testdata", "malformed_macho"), 0, 0, 0)
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

func TestOpenELF(t *testing.T) {
	// The exe_linux_64 has two loadable program headers:
	//  LOAD           0x0000000000000000 0x0000000000400000 0x0000000000400000
	//                 0x00000000000006fc 0x00000000000006fc  R E    0x200000
	//  LOAD           0x0000000000000e10 0x0000000000600e10 0x0000000000600e10
	//                 0x0000000000000230 0x0000000000000238  RW     0x200000
	name := filepath.Join("testdata", "exe_linux_64")

	for _, tc := range []struct {
		desc                 string
		start, limit, offset uint64
		wantError            bool
		wantBase             uint64
	}{
		{"exec mapping", 0x5400000, 0x5401000, 0, false, 0x5000000},
	} {
		t.Run(tc.desc, func(t *testing.T) {
			b := binrep{}
			o, err := b.openELF(name, tc.start, tc.limit, tc.offset)
			if (err != nil) != tc.wantError {
				t.Errorf("got error %v, want any error=%v", err, tc.wantError)
			}
			if err != nil {
				return
			}
			if got := o.Base(); got != tc.wantBase {
				t.Errorf("got base %x; want %x\n", got, tc.wantBase)
			}
		})
	}
}
