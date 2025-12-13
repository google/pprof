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
	"fmt"
	"reflect"
	"strings"
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
	// Kernel PIE header with vaddr aligned to a 4k boundary
	kernelPieAlignedHeader := &elf.ProgHeader{
		Vaddr: 0xffff800010010000,
		Off:   0x10000,
	}
	// Kernel PIE header with vaddr that doesn't fall on a 4k boundary
	kernelPieUnalignedHeader := &elf.ProgHeader{
		Vaddr: 0xffffffc010080800,
		Off:   0x10800,
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
		{"dyn kernel", fhDyn, kernelPieAlignedHeader, uint64p(0xffff800010000000), 0xffff800010000000, 0xffff800012815c00, 0xffff800010000000, 0, false},
		{"dyn chromeos aslr kernel", fhDyn, kernelPieUnalignedHeader, uint64p(0xffffffc010080800), 0x800, 0xb7f800, 0, 0x3feff80000, false},
		{"dyn chromeos aslr kernel unremapped", fhDyn, kernelPieUnalignedHeader, uint64p(0xffffffc010080800), 0xffffffdb5d680800, 0xffffffdb5e200000, 0xffffffdb5d680800, 0x1b4d600000, false},
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
	buildList := func(headers []*elf.ProgHeader) (result string) {
		builder := strings.Builder{}
		if err := builder.WriteByte('['); err != nil {
			t.Error("Failed to append '[' to the builder")
		}
		defer func() {
			if err := builder.WriteByte(']'); err != nil {
				t.Error("Failed to append ']' to the builder")
			}
			result = builder.String()
		}()
		if len(headers) == 0 {
			if _, err := builder.WriteString("nil"); err != nil {
				t.Error("Failed to append 'nil' to the builder")
			}
			return
		}
		if _, err := builder.WriteString(fmt.Sprintf("%#v", *headers[0])); err != nil {
			t.Error("Failed to append first header to the builder")
		}
		for i, h := range headers[1:] {
			if _, err := builder.WriteString(fmt.Sprintf(", %#v", *h)); err != nil {
				t.Errorf("Failed to append header %d to the builder", i+1)
			}
		}
		return
	}

	// Various ELF program headers for unit tests.
	tinyHeaders := []elf.ProgHeader{
		{Type: elf.PT_LOAD, Flags: elf.PF_R | elf.PF_X, Off: 0, Vaddr: 0, Paddr: 0, Filesz: 0xc80, Memsz: 0xc80, Align: 0x200000},
		{Type: elf.PT_LOAD, Flags: elf.PF_R | elf.PF_W, Off: 0xc80, Vaddr: 0x200c80, Paddr: 0x200c80, Filesz: 0x1f0, Memsz: 0x1f0, Align: 0x200000},
	}
	tinyBadBSSHeaders := []elf.ProgHeader{
		{Type: elf.PT_LOAD, Flags: elf.PF_R | elf.PF_X, Off: 0, Vaddr: 0, Paddr: 0, Filesz: 0xc80, Memsz: 0xc80, Align: 0x200000},
		{Type: elf.PT_LOAD, Flags: elf.PF_R | elf.PF_W, Off: 0xc80, Vaddr: 0x200c80, Paddr: 0x200c80, Filesz: 0x100, Memsz: 0x1f0, Align: 0x200000},
		{Type: elf.PT_LOAD, Flags: elf.PF_R | elf.PF_W, Off: 0xd80, Vaddr: 0x400d80, Paddr: 0x400d80, Filesz: 0x90, Memsz: 0x90, Align: 0x200000},
	}
	smallHeaders := []elf.ProgHeader{
		{Type: elf.PT_PHDR, Flags: elf.PF_R | elf.PF_X, Off: 0x40, Vaddr: 0x400040, Paddr: 0x400040, Filesz: 0x1f8, Memsz: 0x1f8, Align: 8},
		{Type: elf.PT_INTERP, Flags: elf.PF_R, Off: 0x238, Vaddr: 0x400238, Paddr: 0x400238, Filesz: 0x1c, Memsz: 0x1c, Align: 1},
		{Type: elf.PT_LOAD, Flags: elf.PF_R | elf.PF_X, Off: 0, Vaddr: 0x400000, Paddr: 0x400000, Filesz: 0x6fc, Memsz: 0x6fc, Align: 0x200000},
		{Type: elf.PT_LOAD, Flags: elf.PF_R | elf.PF_W, Off: 0xe10, Vaddr: 0x600e10, Paddr: 0x600e10, Filesz: 0x230, Memsz: 0x238, Align: 0x200000},
		{Type: elf.PT_DYNAMIC, Flags: elf.PF_R | elf.PF_W, Off: 0xe28, Vaddr: 0x600e28, Paddr: 0x600e28, Filesz: 0x1d0, Memsz: 0x1d0, Align: 8},
	}
	smallBadBSSHeaders := []elf.ProgHeader{
		{Type: elf.PT_LOAD, Flags: elf.PF_R | elf.PF_X, Off: 0, Vaddr: 0x200000, Paddr: 0x200000, Filesz: 0x6fc, Memsz: 0x6fc, Align: 0x200000},
		{Type: elf.PT_LOAD, Flags: elf.PF_R | elf.PF_W, Off: 0x700, Vaddr: 0x400700, Paddr: 0x400700, Filesz: 0x500, Memsz: 0x710, Align: 0x200000},
		{Type: elf.PT_LOAD, Flags: elf.PF_R | elf.PF_W, Off: 0xe10, Vaddr: 0x600e10, Paddr: 0x600e10, Filesz: 0x230, Memsz: 0x238, Align: 0x200000},
	}
	mediumHeaders := []elf.ProgHeader{
		{Type: elf.PT_PHDR, Flags: elf.PF_R, Off: 0x40, Vaddr: 0x40, Paddr: 0x40, Filesz: 0x2d8, Memsz: 0x2d8, Align: 8},
		{Type: elf.PT_INTERP, Flags: elf.PF_R, Off: 0x318, Vaddr: 0x318, Paddr: 0x318, Filesz: 0x28, Memsz: 0x28, Align: 1},
		{Type: elf.PT_LOAD, Flags: elf.PF_R | elf.PF_X, Off: 0, Vaddr: 0, Paddr: 0, Filesz: 0x354ca0, Memsz: 0x354ca0, Align: 0x200000},
		{Type: elf.PT_LOAD, Flags: elf.PF_R | elf.PF_W, Off: 0x400000, Vaddr: 600000, Paddr: 600000, Filesz: 0x0083b8, Memsz: 0x009000, Align: 0x200000},
		{Type: elf.PT_LOAD, Flags: elf.PF_R | elf.PF_W, Off: 0x600000, Vaddr: 0xa00000, Paddr: 0xa00000, Filesz: 0x007030, Memsz: 0x9cf090, Align: 0x200000},
		{Type: elf.PT_LOAD, Flags: elf.PF_R, Off: 0x7cf090, Vaddr: 0x15cf090, Paddr: 0x15cf090, Filesz: 0x028460, Memsz: 0x028460, Align: 0x200000},
		{Type: elf.PT_LOAD, Flags: elf.PF_R | elf.PF_W, Off: 0x7f7500, Vaddr: 0x17f7500, Paddr: 0x17f7500, Filesz: 0x001f80, Memsz: 0x5bdd9a0, Align: 0x200000},
		{Type: elf.PT_TLS, Flags: elf.PF_R, Off: 0x407740, Vaddr: 0x607740, Paddr: 0x607740, Filesz: 0x98, Memsz: 0x350, Align: 0x40},
		{Type: elf.PT_DYNAMIC, Flags: elf.PF_R | elf.PF_W, Off: 0x407920, Vaddr: 0x607920, Paddr: 0x607920, Filesz: 0x230, Memsz: 0x230, Align: 8},
	}
	largeHeaders := []elf.ProgHeader{
		{Type: elf.PT_PHDR, Flags: elf.PF_R, Off: 0x40, Vaddr: 0x40, Paddr: 0x40, Filesz: 0x268, Memsz: 0x268, Align: 8},
		{Type: elf.PT_INTERP, Flags: elf.PF_R, Off: 0x2a8, Vaddr: 0x2a8, Paddr: 0x2a8, Filesz: 0x28, Memsz: 0x28, Align: 1},
		{Type: elf.PT_LOAD, Flags: elf.PF_R | elf.PF_X, Off: 0, Vaddr: 0, Paddr: 0, Filesz: 0x2ec5d2c0, Memsz: 0x2ec5d2c0, Align: 0x200000},
		{Type: elf.PT_LOAD, Flags: elf.PF_R | elf.PF_W, Off: 0x2ec5d2c0, Vaddr: 0x2ee5d2c0, Paddr: 0x2ee5d2c0, Filesz: 0x1361118, Memsz: 0x1361150, Align: 0x200000},
		{Type: elf.PT_LOAD, Flags: elf.PF_R | elf.PF_W, Off: 0x2ffbe440, Vaddr: 0x303be440, Paddr: 0x303be440, Filesz: 0x4637c0, Memsz: 0xc91610, Align: 0x200000},
		{Type: elf.PT_TLS, Flags: elf.PF_R, Off: 0x2ec5d2c0, Vaddr: 0x2ee5d2c0, Paddr: 0x2ee5d2c0, Filesz: 0x120, Memsz: 0x103f8, Align: 0x40},
		{Type: elf.PT_DYNAMIC, Flags: elf.PF_R | elf.PF_W, Off: 0x2ffbc9e0, Vaddr: 0x301bc9e0, Paddr: 0x301bc9e0, Filesz: 0x1f0, Memsz: 0x1f0, Align: 8},
	}
	largeHeadersWithRoSegment := []elf.ProgHeader{
		{Type: elf.PT_PHDR, Flags: elf.PF_R, Off: 0x40, Vaddr: 0x40, Paddr: 0x40, Filesz: 0x348, Memsz: 0x348, Align: 8},
		{Type: elf.PT_INTERP, Flags: elf.PF_R, Off: 0x388, Vaddr: 0x388, Paddr: 0x388, Filesz: 0x28, Memsz: 0x28, Align: 1},
		{Type: elf.PT_LOAD, Flags: elf.PF_R, Off: 0, Vaddr: 0, Paddr: 0, Filesz: 0x81c5628, Memsz: 0x81c5628, Align: 0x200000},
		{Type: elf.PT_LOAD, Flags: elf.PF_R | elf.PF_W, Off: 0x81c6000, Vaddr: 0x83c6000, Paddr: 0x83c6000, Filesz: 0x1af7fb0, Memsz: 0x1af7fb0, Align: 0x200000},
		{Type: elf.PT_LOAD, Flags: elf.PF_R | elf.PF_X, Off: 0x9cbdfc0, Vaddr: 0xa0bdfc0, Paddr: 0xa0bdfc0, Filesz: 0x16e68fc0, Memsz: 0x16e68fc0, Align: 0x200000},
		{Type: elf.PT_LOAD, Flags: elf.PF_R | elf.PF_W, Off: 0x20c00000, Vaddr: 0x21200000, Paddr: 0x21200000, Filesz: 0x1c602f8, Memsz: 0x1c61000, Align: 0x200000},
		{Type: elf.PT_LOAD, Flags: elf.PF_R | elf.PF_W, Off: 0x22a00000, Vaddr: 0x23200000, Paddr: 0x23200000, Filesz: 0x120ed30, Memsz: 0x1d2c2e0, Align: 0x200000},
		{Type: elf.PT_LOAD, Flags: elf.PF_R, Off: 0x23d2c2e0, Vaddr: 0x2512c2e0, Paddr: 0x2512c2e0, Filesz: 0x2cbc4dc, Memsz: 0x2cbc4dc, Align: 0x200000},
		{Type: elf.PT_LOAD, Flags: elf.PF_R | elf.PF_W, Off: 0x269e87c0, Vaddr: 0x27fe87c0, Paddr: 0x27fe87c0, Filesz: 0x6f43c8, Memsz: 0x736e00, Align: 0x200000},
		{Type: elf.PT_TLS, Flags: elf.PF_R, Off: 0x22840200, Vaddr: 0x22e40200, Paddr: 0x22e40200, Filesz: 0x298, Memsz: 0x5428, Align: 0x40},
		{Type: elf.PT_DYNAMIC, Flags: elf.PF_R | elf.PF_W, Off: 0x2285daa0, Vaddr: 0x22e5daa0, Paddr: 0x22e5daa0, Filesz: 0x250, Memsz: 0x250, Align: 8},
	}
	ffmpegHeaders := []elf.ProgHeader{
		{Type: elf.PT_PHDR, Flags: elf.PF_R, Off: 0x40, Vaddr: 0x200040, Paddr: 0x200040, Filesz: 0x1f8, Memsz: 0x1f8, Align: 8},
		{Type: elf.PT_INTERP, Flags: elf.PF_R, Off: 0x238, Vaddr: 0x200238, Paddr: 0x200238, Filesz: 0x28, Memsz: 0x28, Align: 1},
		{Type: elf.PT_LOAD, Flags: elf.PF_R | elf.PF_X, Off: 0, Vaddr: 0x200000, Paddr: 0x200000, Filesz: 0x48d8410, Memsz: 0x48d8410, Align: 0x200000},
		{Type: elf.PT_LOAD, Flags: elf.PF_R | elf.PF_W, Off: 0x48d8440, Vaddr: 0x4cd8440, Paddr: 0x4cd8440, Filesz: 0x18cbe0, Memsz: 0xd2fb70, Align: 0x200000},
		{Type: elf.PT_TLS, Flags: elf.PF_R, Off: 0x48d8440, Vaddr: 0x4cd8440, Paddr: 0x4cd8440, Filesz: 0xa8, Memsz: 0x468, Align: 0x40},
		{Type: elf.PT_DYNAMIC, Flags: elf.PF_R | elf.PF_W, Off: 0x4a63ad0, Vaddr: 0x4e63ad0, Paddr: 0x4e63ad0, Filesz: 0x200, Memsz: 0x200, Align: 8},
	}
	sentryHeaders := []elf.ProgHeader{
		{Type: elf.PT_LOAD, Flags: elf.PF_X + elf.PF_R, Off: 0x0, Vaddr: 0x7f0000000000, Paddr: 0x7f0000000000, Filesz: 0xbc64d5, Memsz: 0xbc64d5, Align: 0x1000},
		{Type: elf.PT_LOAD, Flags: elf.PF_R, Off: 0xbc7000, Vaddr: 0x7f0000bc7000, Paddr: 0x7f0000bc7000, Filesz: 0xcd6b30, Memsz: 0xcd6b30, Align: 0x1000},
		{Type: elf.PT_LOAD, Flags: elf.PF_W + elf.PF_R, Off: 0x189e000, Vaddr: 0x7f000189e000, Paddr: 0x7f000189e000, Filesz: 0x58180, Memsz: 0x92d10, Align: 0x1000},
	}

	for _, tc := range []struct {
		desc        string
		phdrs       []elf.ProgHeader
		pgoff       uint64
		memsz       uint64
		wantHeaders []*elf.ProgHeader
	}{
		{
			desc:        "no prog headers",
			phdrs:       nil,
			pgoff:       0,
			memsz:       0x1000,
			wantHeaders: nil,
		},
		{
			desc:  "tiny file, 4KB at offset 0 matches both headers, b/178747588",
			phdrs: tinyHeaders,
			pgoff: 0,
			memsz: 0x1000,
			wantHeaders: []*elf.ProgHeader{
				{Type: elf.PT_LOAD, Flags: elf.PF_R | elf.PF_X, Off: 0, Vaddr: 0, Paddr: 0, Filesz: 0xc80, Memsz: 0xc80, Align: 0x200000},
				{Type: elf.PT_LOAD, Flags: elf.PF_R | elf.PF_W, Off: 0xc80, Vaddr: 0x200c80, Paddr: 0x200c80, Filesz: 0x1f0, Memsz: 0x1f0, Align: 0x200000},
			},
		},
		{
			desc:        "tiny file, file offset 4KB matches no headers",
			phdrs:       tinyHeaders,
			pgoff:       0x1000,
			memsz:       0x1000,
			wantHeaders: nil,
		},
		{
			desc:        "tiny file with unaligned memsz matches executable segment",
			phdrs:       tinyHeaders,
			pgoff:       0,
			memsz:       0xc80,
			wantHeaders: []*elf.ProgHeader{{Type: elf.PT_LOAD, Flags: elf.PF_R | elf.PF_X, Off: 0, Vaddr: 0, Paddr: 0, Filesz: 0xc80, Memsz: 0xc80, Align: 0x200000}},
		},
		{
			desc:        "tiny file with unaligned offset matches data segment",
			phdrs:       tinyHeaders,
			pgoff:       0xc80,
			memsz:       0x1000,
			wantHeaders: []*elf.ProgHeader{{Type: elf.PT_LOAD, Flags: elf.PF_R | elf.PF_W, Off: 0xc80, Vaddr: 0x200c80, Paddr: 0x200c80, Filesz: 0x1f0, Memsz: 0x1f0, Align: 0x200000}},
		},
		{
			desc:  "tiny bad BSS file, 4KB at offset 0 matches all three headers",
			phdrs: tinyBadBSSHeaders,
			pgoff: 0,
			memsz: 0x1000,
			wantHeaders: []*elf.ProgHeader{
				{Type: elf.PT_LOAD, Flags: elf.PF_R | elf.PF_X, Off: 0, Vaddr: 0, Paddr: 0, Filesz: 0xc80, Memsz: 0xc80, Align: 0x200000},
				{Type: elf.PT_LOAD, Flags: elf.PF_R | elf.PF_W, Off: 0xc80, Vaddr: 0x200c80, Paddr: 0x200c80, Filesz: 0x100, Memsz: 0x1f0, Align: 0x200000},
				{Type: elf.PT_LOAD, Flags: elf.PF_R | elf.PF_W, Off: 0xd80, Vaddr: 0x400d80, Paddr: 0x400d80, Filesz: 0x90, Memsz: 0x90, Align: 0x200000},
			},
		},
		{
			desc:  "small file, offset 0, memsz 4KB matches both segments",
			phdrs: smallHeaders,
			pgoff: 0,
			memsz: 0x1000,
			wantHeaders: []*elf.ProgHeader{
				{Type: elf.PT_LOAD, Flags: elf.PF_R | elf.PF_X, Off: 0, Vaddr: 0x400000, Paddr: 0x400000, Filesz: 0x6fc, Memsz: 0x6fc, Align: 0x200000},
				{Type: elf.PT_LOAD, Flags: elf.PF_R | elf.PF_W, Off: 0xe10, Vaddr: 0x600e10, Paddr: 0x600e10, Filesz: 0x230, Memsz: 0x238, Align: 0x200000},
			},
		},
		{
			desc:  "small file, offset 0, memsz 8KB matches both segments",
			phdrs: smallHeaders,
			pgoff: 0,
			memsz: 0x2000,
			wantHeaders: []*elf.ProgHeader{
				{Type: elf.PT_LOAD, Flags: elf.PF_R | elf.PF_X, Off: 0, Vaddr: 0x400000, Paddr: 0x400000, Filesz: 0x6fc, Memsz: 0x6fc, Align: 0x200000},
				{Type: elf.PT_LOAD, Flags: elf.PF_R | elf.PF_W, Off: 0xe10, Vaddr: 0x600e10, Paddr: 0x600e10, Filesz: 0x230, Memsz: 0x238, Align: 0x200000},
			},
		},
		{
			desc:        "small file, offset 4KB matches data segment",
			phdrs:       smallHeaders,
			pgoff:       0x1000,
			memsz:       0x1000,
			wantHeaders: []*elf.ProgHeader{{Type: elf.PT_LOAD, Flags: elf.PF_R | elf.PF_W, Off: 0xe10, Vaddr: 0x600e10, Paddr: 0x600e10, Filesz: 0x230, Memsz: 0x238, Align: 0x200000}},
		},
		{
			desc:        "small file, offset 8KB matches no segment",
			phdrs:       smallHeaders,
			pgoff:       0x2000,
			memsz:       0x1000,
			wantHeaders: nil,
		},
		{
			desc:  "small bad BSS file, offset 0, memsz 4KB matches all three segments",
			phdrs: smallBadBSSHeaders,
			pgoff: 0,
			memsz: 0x1000,
			wantHeaders: []*elf.ProgHeader{
				{Type: elf.PT_LOAD, Flags: elf.PF_R | elf.PF_X, Off: 0, Vaddr: 0x200000, Paddr: 0x200000, Filesz: 0x6fc, Memsz: 0x6fc, Align: 0x200000},
				{Type: elf.PT_LOAD, Flags: elf.PF_R | elf.PF_W, Off: 0x700, Vaddr: 0x400700, Paddr: 0x400700, Filesz: 0x500, Memsz: 0x710, Align: 0x200000},
				{Type: elf.PT_LOAD, Flags: elf.PF_R | elf.PF_W, Off: 0xe10, Vaddr: 0x600e10, Paddr: 0x600e10, Filesz: 0x230, Memsz: 0x238, Align: 0x200000},
			},
		},
		{
			desc:  "small bad BSS file, offset 0, memsz 8KB matches all three segments",
			phdrs: smallBadBSSHeaders,
			pgoff: 0,
			memsz: 0x2000,
			wantHeaders: []*elf.ProgHeader{
				{Type: elf.PT_LOAD, Flags: elf.PF_R | elf.PF_X, Off: 0, Vaddr: 0x200000, Paddr: 0x200000, Filesz: 0x6fc, Memsz: 0x6fc, Align: 0x200000},
				{Type: elf.PT_LOAD, Flags: elf.PF_R | elf.PF_W, Off: 0x700, Vaddr: 0x400700, Paddr: 0x400700, Filesz: 0x500, Memsz: 0x710, Align: 0x200000},
				{Type: elf.PT_LOAD, Flags: elf.PF_R | elf.PF_W, Off: 0xe10, Vaddr: 0x600e10, Paddr: 0x600e10, Filesz: 0x230, Memsz: 0x238, Align: 0x200000},
			},
		},
		{
			desc:        "small bad BSS file, offset 4KB matches second data segment",
			phdrs:       smallBadBSSHeaders,
			pgoff:       0x1000,
			memsz:       0x1000,
			wantHeaders: []*elf.ProgHeader{{Type: elf.PT_LOAD, Flags: elf.PF_R | elf.PF_W, Off: 0xe10, Vaddr: 0x600e10, Paddr: 0x600e10, Filesz: 0x230, Memsz: 0x238, Align: 0x200000}},
		},
		{
			desc:        "medium file large mapping that includes all address space matches executable segment, b/179920361",
			phdrs:       mediumHeaders,
			pgoff:       0,
			memsz:       0x73d5000,
			wantHeaders: []*elf.ProgHeader{{Type: elf.PT_LOAD, Flags: elf.PF_R | elf.PF_X, Off: 0, Vaddr: 0, Paddr: 0, Filesz: 0x354ca0, Memsz: 0x354ca0, Align: 0x200000}},
		},
		{
			desc:        "large file executable mapping matches executable segment",
			phdrs:       largeHeaders,
			pgoff:       0,
			memsz:       0x2ec5e000,
			wantHeaders: []*elf.ProgHeader{{Type: elf.PT_LOAD, Flags: elf.PF_R | elf.PF_X, Off: 0, Vaddr: 0, Paddr: 0, Filesz: 0x2ec5d2c0, Memsz: 0x2ec5d2c0, Align: 0x200000}},
		},
		{
			desc:        "large file first data mapping matches first data segment",
			phdrs:       largeHeaders,
			pgoff:       0x2ec5d000,
			memsz:       0x1362000,
			wantHeaders: []*elf.ProgHeader{{Type: elf.PT_LOAD, Flags: elf.PF_R | elf.PF_W, Off: 0x2ec5d2c0, Vaddr: 0x2ee5d2c0, Paddr: 0x2ee5d2c0, Filesz: 0x1361118, Memsz: 0x1361150, Align: 0x200000}},
		},
		{
			desc:        "large file, split second data mapping matches second data segment",
			phdrs:       largeHeaders,
			pgoff:       0x2ffbe000,
			memsz:       0xb11000,
			wantHeaders: []*elf.ProgHeader{{Type: elf.PT_LOAD, Flags: elf.PF_R | elf.PF_W, Off: 0x2ffbe440, Vaddr: 0x303be440, Paddr: 0x303be440, Filesz: 0x4637c0, Memsz: 0xc91610, Align: 0x200000}},
		},
		{
			desc:  "large file with RO segment executable mapping includes executable segment",
			phdrs: largeHeadersWithRoSegment,
			pgoff: 0x9c00000,
			memsz: 0x16f27000,
			wantHeaders: []*elf.ProgHeader{
				{Type: elf.PT_LOAD, Flags: elf.PF_R | elf.PF_W, Off: 0x81c6000, Vaddr: 0x83c6000, Paddr: 0x83c6000, Filesz: 0x1af7fb0, Memsz: 0x1af7fb0, Align: 0x200000},
				{Type: elf.PT_LOAD, Flags: elf.PF_R | elf.PF_X, Off: 0x9cbdfc0, Vaddr: 0xa0bdfc0, Paddr: 0xa0bdfc0, Filesz: 0x16e68fc0, Memsz: 0x16e68fc0, Align: 0x200000},
			},
		},
		{
			desc:        "sentry headers, mapping for last page of executable segment matches executable segment",
			phdrs:       sentryHeaders,
			pgoff:       0xbc6000,
			memsz:       0x1000,
			wantHeaders: []*elf.ProgHeader{{Type: elf.PT_LOAD, Flags: elf.PF_X + elf.PF_R, Off: 0x0, Vaddr: 0x7f0000000000, Paddr: 0x7f0000000000, Filesz: 0xbc64d5, Memsz: 0xbc64d5, Align: 0x1000}},
		},
		{
			desc:        "ffmpeg headers, split mapping for executable segment matches executable segment, b/193176694",
			phdrs:       ffmpegHeaders,
			pgoff:       0,
			memsz:       0x48d8000,
			wantHeaders: []*elf.ProgHeader{{Type: elf.PT_LOAD, Flags: elf.PF_R | elf.PF_X, Off: 0, Vaddr: 0x200000, Paddr: 0x200000, Filesz: 0x48d8410, Memsz: 0x48d8410, Align: 0x200000}},
		},
		{
			desc: "segments with no file bits (b/195427553), mapping for executable segment matches executable segment",
			phdrs: []elf.ProgHeader{
				{Type: elf.PT_LOAD, Flags: elf.PF_R, Off: 0x0, Vaddr: 0x0, Paddr: 0x0, Filesz: 0x115000, Memsz: 0x115000, Align: 0x1000},
				{Type: elf.PT_LOAD, Flags: elf.PF_X + elf.PF_R, Off: 0x115000, Vaddr: 0x115000, Paddr: 0x115000, Filesz: 0x361e15, Memsz: 0x361e15, Align: 0x1000},
				{Type: elf.PT_LOAD, Flags: elf.PF_W + elf.PF_R, Off: 0x0, Vaddr: 0x477000, Paddr: 0x477000, Filesz: 0x0, Memsz: 0x33c, Align: 0x1000},
				{Type: elf.PT_LOAD, Flags: elf.PF_R, Off: 0x0, Vaddr: 0x478000, Paddr: 0x478000, Filesz: 0x0, Memsz: 0x47dc28, Align: 0x1000},
				{Type: elf.PT_LOAD, Flags: elf.PF_R, Off: 0x477000, Vaddr: 0x8f6000, Paddr: 0x8f6000, Filesz: 0x140, Memsz: 0x140, Align: 0x1000},
				{Type: elf.PT_LOAD, Flags: elf.PF_W + elf.PF_R, Off: 0x478000, Vaddr: 0x8f7000, Paddr: 0x8f7000, Filesz: 0x38, Memsz: 0x38, Align: 0x1000},
			},
			pgoff:       0x115000,
			memsz:       0x362000,
			wantHeaders: []*elf.ProgHeader{{Type: elf.PT_LOAD, Flags: elf.PF_X + elf.PF_R, Off: 0x115000, Vaddr: 0x115000, Paddr: 0x115000, Filesz: 0x361e15, Memsz: 0x361e15, Align: 0x1000}},
		},
	} {
		t.Run(tc.desc, func(t *testing.T) {
			gotHeaders := ProgramHeadersForMapping(tc.phdrs, tc.pgoff, tc.memsz)
			if !reflect.DeepEqual(gotHeaders, tc.wantHeaders) {
				t.Errorf("got program headers %q; want %q", buildList(gotHeaders), buildList(tc.wantHeaders))
			}
		})
	}
}

func TestHeaderForFileOffset(t *testing.T) {
	for _, tc := range []struct {
		desc       string
		headers    []*elf.ProgHeader
		fileOffset uint64
		wantError  bool
		want       *elf.ProgHeader
	}{
		{
			desc:      "no headers, want error",
			headers:   nil,
			wantError: true,
		},
		{
			desc: "three headers, BSS in last segment, file offset selects first header",
			headers: []*elf.ProgHeader{
				{Type: elf.PT_LOAD, Flags: elf.PF_R | elf.PF_X, Off: 0, Vaddr: 0, Paddr: 0, Filesz: 0xc80, Memsz: 0xc80, Align: 0x200000},
				{Type: elf.PT_LOAD, Flags: elf.PF_R | elf.PF_W, Off: 0xc80, Vaddr: 0x200c80, Paddr: 0x200c80, Filesz: 0x1f0, Memsz: 0x1f0, Align: 0x200000},
				{Type: elf.PT_LOAD, Flags: elf.PF_R | elf.PF_W, Off: 0xe70, Vaddr: 0x400e70, Paddr: 0x400e70, Filesz: 0x90, Memsz: 0x100, Align: 0x200000},
			},
			fileOffset: 0xc79,
			want:       &elf.ProgHeader{Type: elf.PT_LOAD, Flags: elf.PF_R | elf.PF_X, Off: 0, Vaddr: 0, Paddr: 0, Filesz: 0xc80, Memsz: 0xc80, Align: 0x200000},
		},
		{
			desc: "three headers, BSS in last segment, file offset selects second header",
			headers: []*elf.ProgHeader{
				{Type: elf.PT_LOAD, Flags: elf.PF_R | elf.PF_X, Off: 0, Vaddr: 0, Paddr: 0, Filesz: 0xc80, Memsz: 0xc80, Align: 0x200000},
				{Type: elf.PT_LOAD, Flags: elf.PF_R | elf.PF_W, Off: 0xc80, Vaddr: 0x200c80, Paddr: 0x200c80, Filesz: 0x1f0, Memsz: 0x1f0, Align: 0x200000},
				{Type: elf.PT_LOAD, Flags: elf.PF_R | elf.PF_W, Off: 0xe70, Vaddr: 0x400e70, Paddr: 0x400e70, Filesz: 0x90, Memsz: 0x100, Align: 0x200000},
			},
			fileOffset: 0xc80,
			want:       &elf.ProgHeader{Type: elf.PT_LOAD, Flags: elf.PF_R | elf.PF_W, Off: 0xc80, Vaddr: 0x200c80, Paddr: 0x200c80, Filesz: 0x1f0, Memsz: 0x1f0, Align: 0x200000},
		},
		{
			desc: "three headers, BSS in last segment, file offset selects third header",
			headers: []*elf.ProgHeader{
				{Type: elf.PT_LOAD, Flags: elf.PF_R | elf.PF_X, Off: 0, Vaddr: 0, Paddr: 0, Filesz: 0xc80, Memsz: 0xc80, Align: 0x200000},
				{Type: elf.PT_LOAD, Flags: elf.PF_R | elf.PF_W, Off: 0xc80, Vaddr: 0x200c80, Paddr: 0x200c80, Filesz: 0x1f0, Memsz: 0x1f0, Align: 0x200000},
				{Type: elf.PT_LOAD, Flags: elf.PF_R | elf.PF_W, Off: 0xe70, Vaddr: 0x400e70, Paddr: 0x400e70, Filesz: 0x90, Memsz: 0x100, Align: 0x200000},
			},
			fileOffset: 0xef0,
			want:       &elf.ProgHeader{Type: elf.PT_LOAD, Flags: elf.PF_R | elf.PF_W, Off: 0xe70, Vaddr: 0x400e70, Paddr: 0x400e70, Filesz: 0x90, Memsz: 0x100, Align: 0x200000},
		},
		{
			desc: "three headers, BSS in last segment, file offset in uninitialized section selects third header",
			headers: []*elf.ProgHeader{
				{Type: elf.PT_LOAD, Flags: elf.PF_R | elf.PF_X, Off: 0, Vaddr: 0, Paddr: 0, Filesz: 0xc80, Memsz: 0xc80, Align: 0x200000},
				{Type: elf.PT_LOAD, Flags: elf.PF_R | elf.PF_W, Off: 0xc80, Vaddr: 0x200c80, Paddr: 0x200c80, Filesz: 0x1f0, Memsz: 0x1f0, Align: 0x200000},
				{Type: elf.PT_LOAD, Flags: elf.PF_R | elf.PF_W, Off: 0xe70, Vaddr: 0x400e70, Paddr: 0x400e70, Filesz: 0x90, Memsz: 0x100, Align: 0x200000},
			},
			fileOffset: 0xf40,
			want:       &elf.ProgHeader{Type: elf.PT_LOAD, Flags: elf.PF_R | elf.PF_W, Off: 0xe70, Vaddr: 0x400e70, Paddr: 0x400e70, Filesz: 0x90, Memsz: 0x100, Align: 0x200000},
		},
		{
			desc: "three headers, BSS in last segment, file offset past any segment gives error",
			headers: []*elf.ProgHeader{
				{Type: elf.PT_LOAD, Flags: elf.PF_R | elf.PF_X, Off: 0, Vaddr: 0, Paddr: 0, Filesz: 0xc80, Memsz: 0xc80, Align: 0x200000},
				{Type: elf.PT_LOAD, Flags: elf.PF_R | elf.PF_W, Off: 0xc80, Vaddr: 0x200c80, Paddr: 0x200c80, Filesz: 0x1f0, Memsz: 0x1f0, Align: 0x200000},
				{Type: elf.PT_LOAD, Flags: elf.PF_R | elf.PF_W, Off: 0xe70, Vaddr: 0x400e70, Paddr: 0x400e70, Filesz: 0x90, Memsz: 0x100, Align: 0x200000},
			},
			fileOffset: 0xf70,
			wantError:  true,
		},
		{
			desc: "three headers, BSS in second segment, file offset in mapped section selects second header",
			headers: []*elf.ProgHeader{
				{Type: elf.PT_LOAD, Flags: elf.PF_R | elf.PF_X, Off: 0, Vaddr: 0, Paddr: 0, Filesz: 0xc80, Memsz: 0xc80, Align: 0x200000},
				{Type: elf.PT_LOAD, Flags: elf.PF_R | elf.PF_W, Off: 0xc80, Vaddr: 0x200c80, Paddr: 0x200c80, Filesz: 0x100, Memsz: 0x1f0, Align: 0x200000},
				{Type: elf.PT_LOAD, Flags: elf.PF_R | elf.PF_W, Off: 0xd80, Vaddr: 0x400d80, Paddr: 0x400d80, Filesz: 0x100, Memsz: 0x100, Align: 0x200000},
			},
			fileOffset: 0xd79,
			want:       &elf.ProgHeader{Type: elf.PT_LOAD, Flags: elf.PF_R | elf.PF_W, Off: 0xc80, Vaddr: 0x200c80, Paddr: 0x200c80, Filesz: 0x100, Memsz: 0x1f0, Align: 0x200000},
		},
		{
			desc: "three headers, BSS in second segment, file offset in unmapped section gives error",
			headers: []*elf.ProgHeader{
				{Type: elf.PT_LOAD, Flags: elf.PF_R | elf.PF_X, Off: 0, Vaddr: 0, Paddr: 0, Filesz: 0xc80, Memsz: 0xc80, Align: 0x200000},
				{Type: elf.PT_LOAD, Flags: elf.PF_R | elf.PF_W, Off: 0xc80, Vaddr: 0x200c80, Paddr: 0x200c80, Filesz: 0x100, Memsz: 0x1f0, Align: 0x200000},
				{Type: elf.PT_LOAD, Flags: elf.PF_R | elf.PF_W, Off: 0xd80, Vaddr: 0x400d80, Paddr: 0x400d80, Filesz: 0x100, Memsz: 0x100, Align: 0x200000},
			},
			fileOffset: 0xd80,
			wantError:  true,
		},
	} {
		t.Run(tc.desc, func(t *testing.T) {
			got, err := HeaderForFileOffset(tc.headers, tc.fileOffset)
			if (err != nil) != tc.wantError {
				t.Errorf("got error %v, want any error=%v", err, tc.wantError)
			}
			if err != nil {
				return
			}
			if !reflect.DeepEqual(got, tc.want) {
				t.Errorf("got program header %#v, want %#v", got, tc.want)
			}
		})
	}
}
