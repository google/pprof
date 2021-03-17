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

package elfexec

import (
	"debug/elf"
	"reflect"
	"testing"
)

func TestGetBase(t *testing.T) {

	fhExec := &elf.FileHeader{
		Type: elf.ET_EXEC,
	}
	fhRel := &elf.FileHeader{
		Type: elf.ET_REL,
	}
	fhDyn := &elf.FileHeader{
		Type: elf.ET_DYN,
	}
	lsOffset := &elf.ProgHeader{
		Vaddr: 0x400000,
		Off:   0x200000,
	}
	kernelHeader := &elf.ProgHeader{
		Vaddr: 0xffffffff81000000,
	}
	kernelAslrHeader := &elf.ProgHeader{
		Vaddr: 0xffffffff80200000,
		Off:   0x1000,
	}
	ppc64KernelHeader := &elf.ProgHeader{
		Vaddr: 0xc000000000000000,
	}

	testcases := []struct {
		label                string
		fh                   *elf.FileHeader
		loadSegment          *elf.ProgHeader
		stextOffset          *uint64
		start, limit, offset uint64
		want                 uint64
		wanterr              bool
	}{
		{"exec", fhExec, nil, nil, 0x400000, 0, 0, 0, false},
		{"exec offset", fhExec, lsOffset, nil, 0x400000, 0x800000, 0, 0x200000, false},
		{"exec offset 2", fhExec, lsOffset, nil, 0x200000, 0x600000, 0, 0, false},
		{"exec nomap", fhExec, nil, nil, 0, 0, 0, 0, false},
		{"exec kernel", fhExec, kernelHeader, uint64p(0xffffffff81000198), 0xffffffff82000198, 0xffffffff83000198, 0, 0x1000000, false},
		{"exec kernel", fhExec, kernelHeader, uint64p(0xffffffff810002b8), 0xffffffff81000000, 0xffffffffa0000000, 0x0, 0x0, false},
		{"exec kernel ASLR", fhExec, kernelHeader, uint64p(0xffffffff810002b8), 0xffffffff81000000, 0xffffffffa0000000, 0xffffffff81000000, 0x0, false},
		// TODO(aalexand): Figure out where this test case exactly comes from and
		// whether it's still relevant.
		{"exec kernel ASLR 2", fhExec, kernelAslrHeader, nil, 0xffffffff83e00000, 0xfffffffffc3fffff, 0x3c00000, 0x3c00000, false},
		{"exec PPC64 kernel", fhExec, ppc64KernelHeader, uint64p(0xc000000000000000), 0xc000000000000000, 0xd00000001a730000, 0x0, 0x0, false},
		{"exec chromeos kernel", fhExec, kernelHeader, uint64p(0xffffffff81000198), 0, 0x10197, 0, 0x7efffe68, false},
		{"exec chromeos kernel 2", fhExec, kernelHeader, uint64p(0xffffffff81000198), 0, 0x10198, 0, 0x7efffe68, false},
		{"exec chromeos kernel 3", fhExec, kernelHeader, uint64p(0xffffffff81000198), 0x198, 0x100000, 0, 0x7f000000, false},
		{"exec chromeos kernel 4", fhExec, kernelHeader, uint64p(0xffffffff81200198), 0x198, 0x100000, 0, 0x7ee00000, false},
		{"exec chromeos kernel unremapped", fhExec, kernelHeader, uint64p(0xffffffff810001c8), 0xffffffff834001c8, 0xffffffffc0000000, 0xffffffff834001c8, 0x2400000, false},
		{"dyn", fhDyn, nil, nil, 0x200000, 0x300000, 0, 0x200000, false},
		{"dyn map", fhDyn, lsOffset, nil, 0x0, 0x300000, 0, 0xFFFFFFFFFFE00000, false},
		{"dyn nomap", fhDyn, nil, nil, 0x0, 0x0, 0, 0, false},
		{"dyn map+offset", fhDyn, lsOffset, nil, 0x900000, 0xa00000, 0x200000, 0x500000, false},
		{"rel", fhRel, nil, nil, 0x2000000, 0x3000000, 0, 0x2000000, false},
		{"rel nomap", fhRel, nil, nil, 0x0, ^uint64(0), 0, 0, false},
		{"rel offset", fhRel, nil, nil, 0x100000, 0x200000, 0x1, 0, true},
	}

	for _, tc := range testcases {
		base, err := GetBase(tc.fh, tc.loadSegment, tc.stextOffset, tc.start, tc.limit, tc.offset)
		if err != nil {
			if !tc.wanterr {
				t.Errorf("%s: want no error, got %v", tc.label, err)
			}
			continue
		}
		if tc.wanterr {
			t.Errorf("%s: want error, got nil", tc.label)
			continue
		}
		if base != tc.want {
			t.Errorf("%s: want 0x%x, got 0x%x", tc.label, tc.want, base)
		}
	}
}

func uint64p(n uint64) *uint64 {
	return &n
}

func TestFindProgHeaderForMapping(t *testing.T) {
	smallELFFile := elf.File{
		Progs: []*elf.Prog{
			{ProgHeader: elf.ProgHeader{Type: elf.PT_PHDR, Flags: elf.PF_R | elf.PF_X, Off: 0x40, Vaddr: 0x400040, Paddr: 0x400040, Filesz: 0x1f8, Memsz: 0x1f8, Align: 8}},
			{ProgHeader: elf.ProgHeader{Type: elf.PT_INTERP, Flags: elf.PF_R, Off: 0x238, Vaddr: 0x400238, Paddr: 0x400238, Filesz: 0x1c, Memsz: 0x1c, Align: 1}},
			{ProgHeader: elf.ProgHeader{Type: elf.PT_LOAD, Flags: elf.PF_R | elf.PF_X, Off: 0, Vaddr: 0x400000, Paddr: 0x400000, Filesz: 0x6fc, Memsz: 0x6fc, Align: 0x200000}},
			{ProgHeader: elf.ProgHeader{Type: elf.PT_LOAD, Flags: elf.PF_R | elf.PF_W, Off: 0xe10, Vaddr: 0x600e10, Paddr: 0x600e10, Filesz: 0x230, Memsz: 0x238, Align: 0x200000}},
			{ProgHeader: elf.ProgHeader{Type: elf.PT_DYNAMIC, Flags: elf.PF_R | elf.PF_W, Off: 0xe28, Vaddr: 0x600e28, Paddr: 0x600e28, Filesz: 0x1d0, Memsz: 0x1d0, Align: 8}},
		},
	}
	mediumELFFile := elf.File{
		Progs: []*elf.Prog{
			{ProgHeader: elf.ProgHeader{Type: elf.PT_PHDR, Flags: elf.PF_R, Off: 0x40, Vaddr: 0x40, Paddr: 0x40, Filesz: 0x268, Memsz: 0x268, Align: 8}},
			{ProgHeader: elf.ProgHeader{Type: elf.PT_INTERP, Flags: elf.PF_R, Off: 0x2a8, Vaddr: 0x2a8, Paddr: 0x2a8, Filesz: 0x28, Memsz: 0x28, Align: 1}},
			{ProgHeader: elf.ProgHeader{Type: elf.PT_LOAD, Flags: elf.PF_R | elf.PF_X, Off: 0, Vaddr: 0, Paddr: 0, Filesz: 0x51800, Memsz: 0x51800, Align: 0x200000}},
			{ProgHeader: elf.ProgHeader{Type: elf.PT_LOAD, Flags: elf.PF_R | elf.PF_W, Off: 0x51800, Vaddr: 0x251800, Paddr: 0x251800, Filesz: 0x24a8, Memsz: 0x24e8, Align: 0x200000}},
			{ProgHeader: elf.ProgHeader{Type: elf.PT_LOAD, Flags: elf.PF_R | elf.PF_W, Off: 0x53d00, Vaddr: 0x453d00, Paddr: 0x453d00, Filesz: 0x13a58, Memsz: 0x91a198, Align: 0x200000}},
			{ProgHeader: elf.ProgHeader{Type: elf.PT_TLS, Flags: elf.PF_R, Off: 0x51800, Vaddr: 0x51800, Paddr: 0x51800, Filesz: 0x0, Memsz: 0x38, Align: 0x8}},
			{ProgHeader: elf.ProgHeader{Type: elf.PT_DYNAMIC, Flags: elf.PF_R | elf.PF_W, Off: 0x51d00, Vaddr: 0x251d00, Paddr: 0x251d00, Filesz: 0x1ef0, Memsz: 0x1ef0, Align: 8}},
		},
	}
	largeELFFile := elf.File{
		Progs: []*elf.Prog{
			{ProgHeader: elf.ProgHeader{Type: elf.PT_PHDR, Flags: elf.PF_R, Off: 0x40, Vaddr: 0x40, Paddr: 0x40, Filesz: 0x268, Memsz: 0x268, Align: 8}},
			{ProgHeader: elf.ProgHeader{Type: elf.PT_INTERP, Flags: elf.PF_R, Off: 0x2a8, Vaddr: 0x2a8, Paddr: 0x2a8, Filesz: 0x28, Memsz: 0x28, Align: 1}},
			{ProgHeader: elf.ProgHeader{Type: elf.PT_LOAD, Flags: elf.PF_R | elf.PF_X, Off: 0, Vaddr: 0, Paddr: 0, Filesz: 0x2ec5d2c0, Memsz: 0x2ec5d2c0, Align: 0x200000}},
			{ProgHeader: elf.ProgHeader{Type: elf.PT_LOAD, Flags: elf.PF_R | elf.PF_W, Off: 0x2ec5d2c0, Vaddr: 0x2ee5d2c0, Paddr: 0x2ee5d2c0, Filesz: 0x1361118, Memsz: 0x1361150, Align: 0x200000}},
			{ProgHeader: elf.ProgHeader{Type: elf.PT_LOAD, Flags: elf.PF_R | elf.PF_W, Off: 0x2ffbe440, Vaddr: 0x303be440, Paddr: 0x303be440, Filesz: 0x4637c0, Memsz: 0xc91610, Align: 0x200000}},
			{ProgHeader: elf.ProgHeader{Type: elf.PT_TLS, Flags: elf.PF_R, Off: 0x2ec5d2c0, Vaddr: 0x2ee5d2c0, Paddr: 0x2ee5d2c0, Filesz: 0x120, Memsz: 0x103f8, Align: 0x40}},
			{ProgHeader: elf.ProgHeader{Type: elf.PT_DYNAMIC, Flags: elf.PF_R | elf.PF_W, Off: 0x2ffbc9e0, Vaddr: 0x301bc9e0, Paddr: 0x301bc9e0, Filesz: 0x1f0, Memsz: 0x1f0, Align: 8}},
		},
	}
	for _, tc := range []struct {
		desc         string
		file         *elf.File
		pgoff, memsz uint64
		wantError    bool
		want         *elf.ProgHeader
	}{
		{
			desc:  "no prog headers ELF file",
			file:  &elf.File{},
			pgoff: 0,
			memsz: 0x1000,
			want:  nil,
		},
		{
			desc:  "small ELF file / executable mapping",
			file:  &smallELFFile,
			pgoff: 0,
			memsz: 0x1000,
			want:  &elf.ProgHeader{Type: elf.PT_LOAD, Flags: elf.PF_R | elf.PF_X, Off: 0, Vaddr: 0x400000, Paddr: 0x400000, Filesz: 0x6fc, Memsz: 0x6fc, Align: 0x200000},
		},
		{
			desc:  "small ELF file / page aligned data mapping disambiguation",
			file:  &smallELFFile,
			pgoff: 0,
			memsz: 0x2000,
			want:  &elf.ProgHeader{Type: elf.PT_LOAD, Flags: elf.PF_R | elf.PF_W, Off: 0xe10, Vaddr: 0x600e10, Paddr: 0x600e10, Filesz: 0x230, Memsz: 0x238, Align: 0x200000},
		},
		{
			desc:      "small ELF file / no matching segment",
			file:      &smallELFFile,
			pgoff:     0x1000,
			memsz:     0x1000,
			wantError: true,
		},
		{
			desc:      "small ELF file / multiple matching segments, but incorrect size ",
			file:      &smallELFFile,
			pgoff:     0,
			memsz:     0x3000,
			wantError: true,
		},
		{
			desc:  "medium ELF file / large mapping includes all address space",
			file:  &mediumELFFile,
			pgoff: 0,
			memsz: 0xd6e000,
			want:  &elf.ProgHeader{Type: elf.PT_LOAD, Flags: elf.PF_R | elf.PF_X, Off: 0, Vaddr: 0, Paddr: 0, Filesz: 0x51800, Memsz: 0x51800, Align: 0x200000},
		},
		{
			desc:  "large ELF file / executable mapping",
			file:  &largeELFFile,
			pgoff: 0,
			memsz: 0x2ec5e000,
			want:  &elf.ProgHeader{Type: elf.PT_LOAD, Flags: elf.PF_R | elf.PF_X, Off: 0, Vaddr: 0, Paddr: 0, Filesz: 0x2ec5d2c0, Memsz: 0x2ec5d2c0, Align: 0x200000},
		},
		{
			desc:  "large ELF file / first data mapping",
			file:  &largeELFFile,
			pgoff: 0x2ec5d000,
			memsz: 0x1362000,
			want:  &elf.ProgHeader{Type: elf.PT_LOAD, Flags: elf.PF_R | elf.PF_W, Off: 0x2ec5d2c0, Vaddr: 0x2ee5d2c0, Paddr: 0x2ee5d2c0, Filesz: 0x1361118, Memsz: 0x1361150, Align: 0x200000},
		},
		{
			desc:      "large ELF file / split mapping doesn't match",
			file:      &largeELFFile,
			pgoff:     0x2ffbe000,
			memsz:     0xb11000,
			wantError: true,
		},
		{
			desc:  "large ELF file / combined mapping matches second data mapping",
			file:  &largeELFFile,
			pgoff: 0x2ffbe000,
			memsz: 0xb11000 + 0x181000,
			want:  &elf.ProgHeader{Type: elf.PT_LOAD, Flags: elf.PF_R | elf.PF_W, Off: 0x2ffbe440, Vaddr: 0x303be440, Paddr: 0x303be440, Filesz: 0x4637c0, Memsz: 0xc91610, Align: 0x200000},
		},
	} {
		t.Run(tc.desc, func(t *testing.T) {
			got, err := FindProgHeaderForMapping(tc.file, tc.pgoff, tc.memsz)
			if (err != nil) != tc.wantError {
				t.Errorf("got error %v, want any error=%v", err, tc.wantError)
			}
			if err != nil {
				return
			}
			if !reflect.DeepEqual(got, tc.want) {
				t.Errorf("got program header %#v; want %#v", got, tc.want)
			}
		})
	}
}
