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

	"github.com/google/pprof/internal/elfexec/testelf"
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

	for _, tc := range []struct {
		desc          string
		file          *elf.File
		pgoff         uint64
		memsz         uint64
		wantHeaders   []*elf.ProgHeader
		wantLoadables bool
	}{
		{
			desc:          "no prog headers ELF file",
			file:          &elf.File{},
			pgoff:         0,
			memsz:         0x1000,
			wantHeaders:   nil,
			wantLoadables: false,
		},
		{
			desc:  "tiny file, 4KB at offset 0 matches both headers",
			file:  &testelf.TinyFile,
			pgoff: 0,
			memsz: 0x1000,
			wantHeaders: []*elf.ProgHeader{
				{Type: elf.PT_LOAD, Flags: elf.PF_R | elf.PF_X, Off: 0, Vaddr: 0, Paddr: 0, Filesz: 0xc80, Memsz: 0xc80, Align: 0x200000},
				{Type: elf.PT_LOAD, Flags: elf.PF_R | elf.PF_W, Off: 0xc80, Vaddr: 0x200c80, Paddr: 0x200c80, Filesz: 0x1f0, Memsz: 0x1f0, Align: 0x200000},
			},
			wantLoadables: true,
		},
		{
			desc:          "tiny file, file offset 4KB matches no headers",
			file:          &testelf.TinyFile,
			pgoff:         0x1000,
			memsz:         0x1000,
			wantHeaders:   nil,
			wantLoadables: true,
		},
		{
			desc:          "tiny file with unaligned memsz matches executable segment",
			file:          &testelf.TinyFile,
			pgoff:         0,
			memsz:         0xc80,
			wantHeaders:   []*elf.ProgHeader{{Type: elf.PT_LOAD, Flags: elf.PF_R | elf.PF_X, Off: 0, Vaddr: 0, Paddr: 0, Filesz: 0xc80, Memsz: 0xc80, Align: 0x200000}},
			wantLoadables: true,
		},
		{
			desc:          "tiny file with unaligned offset matches data segment",
			file:          &testelf.TinyFile,
			pgoff:         0xc80,
			memsz:         0x1000,
			wantHeaders:   []*elf.ProgHeader{{Type: elf.PT_LOAD, Flags: elf.PF_R | elf.PF_W, Off: 0xc80, Vaddr: 0x200c80, Paddr: 0x200c80, Filesz: 0x1f0, Memsz: 0x1f0, Align: 0x200000}},
			wantLoadables: true,
		},
		{
			desc:  "tiny bad BSS file, 4KB at offset 0 matches all three headers",
			file:  &testelf.TinyBadBSSFile,
			pgoff: 0,
			memsz: 0x1000,
			wantHeaders: []*elf.ProgHeader{
				{Type: elf.PT_LOAD, Flags: elf.PF_R | elf.PF_X, Off: 0, Vaddr: 0, Paddr: 0, Filesz: 0xc80, Memsz: 0xc80, Align: 0x200000},
				{Type: elf.PT_LOAD, Flags: elf.PF_R | elf.PF_W, Off: 0xc80, Vaddr: 0x200c80, Paddr: 0x200c80, Filesz: 0x100, Memsz: 0x1f0, Align: 0x200000},
				{Type: elf.PT_LOAD, Flags: elf.PF_R | elf.PF_W, Off: 0xd80, Vaddr: 0x400d80, Paddr: 0x400d80, Filesz: 0x90, Memsz: 0x90, Align: 0x200000},
			},
			wantLoadables: true,
		},
		{
			desc:          "small file, offset 0, memsz 4KB matches executable segment",
			file:          &testelf.SmallFile,
			pgoff:         0,
			memsz:         0x1000,
			wantHeaders:   []*elf.ProgHeader{{Type: elf.PT_LOAD, Flags: elf.PF_R | elf.PF_X, Off: 0, Vaddr: 0x400000, Paddr: 0x400000, Filesz: 0x6fc, Memsz: 0x6fc, Align: 0x200000}},
			wantLoadables: true,
		},
		{
			desc:          "small file, offset 0, memsz 8KB matches data segment",
			file:          &testelf.SmallFile,
			pgoff:         0,
			memsz:         0x2000,
			wantHeaders:   []*elf.ProgHeader{{Type: elf.PT_LOAD, Flags: elf.PF_R | elf.PF_W, Off: 0xe10, Vaddr: 0x600e10, Paddr: 0x600e10, Filesz: 0x230, Memsz: 0x238, Align: 0x200000}},
			wantLoadables: true,
		},
		{
			desc:          "small file, offset 4KB matches no segment",
			file:          &testelf.SmallFile,
			pgoff:         0x1000,
			memsz:         0x1000,
			wantHeaders:   nil,
			wantLoadables: true,
		},
		{
			desc:  "small file, offset 0, memsz 12KB matches both segments",
			file:  &testelf.SmallFile,
			pgoff: 0,
			memsz: 0x3000,
			wantHeaders: []*elf.ProgHeader{
				{Type: elf.PT_LOAD, Flags: elf.PF_R | elf.PF_X, Off: 0, Vaddr: 0x400000, Paddr: 0x400000, Filesz: 0x6fc, Memsz: 0x6fc, Align: 0x200000},
				{Type: elf.PT_LOAD, Flags: elf.PF_R | elf.PF_W, Off: 0xe10, Vaddr: 0x600e10, Paddr: 0x600e10, Filesz: 0x230, Memsz: 0x238, Align: 0x200000},
			},
			wantLoadables: true,
		},
		{
			desc:  "small bad BSS file, offset 0, memsz 4KB matches two segments",
			file:  &testelf.SmallBadBSSFile,
			pgoff: 0,
			memsz: 0x1000,
			wantHeaders: []*elf.ProgHeader{
				{Type: elf.PT_LOAD, Flags: elf.PF_R | elf.PF_X, Off: 0, Vaddr: 0x200000, Paddr: 0x200000, Filesz: 0x6fc, Memsz: 0x6fc, Align: 0x200000},
				{Type: elf.PT_LOAD, Flags: elf.PF_R | elf.PF_W, Off: 0x700, Vaddr: 0x400700, Paddr: 0x400700, Filesz: 0x500, Memsz: 0x710, Align: 0x200000},
			},
			wantLoadables: true,
		},
		{
			desc:          "small bad BSS file, offset 0, memsz 8KB matches second data segment",
			file:          &testelf.SmallBadBSSFile,
			pgoff:         0,
			memsz:         0x2000,
			wantHeaders:   []*elf.ProgHeader{{Type: elf.PT_LOAD, Flags: elf.PF_R | elf.PF_W, Off: 0xe10, Vaddr: 0x600e10, Paddr: 0x600e10, Filesz: 0x230, Memsz: 0x238, Align: 0x200000}},
			wantLoadables: true,
		},
		{
			desc:          "small bad BSS file, offset 4KB matches no segment",
			file:          &testelf.SmallBadBSSFile,
			pgoff:         0x1000,
			memsz:         0x1000,
			wantHeaders:   nil,
			wantLoadables: true,
		},
		{
			desc:  "small bad BSS file, offset 0, memsz 12KB matches all three segments",
			file:  &testelf.SmallBadBSSFile,
			pgoff: 0,
			memsz: 0x3000,
			wantHeaders: []*elf.ProgHeader{
				{Type: elf.PT_LOAD, Flags: elf.PF_R | elf.PF_X, Off: 0, Vaddr: 0x200000, Paddr: 0x200000, Filesz: 0x6fc, Memsz: 0x6fc, Align: 0x200000},
				{Type: elf.PT_LOAD, Flags: elf.PF_R | elf.PF_W, Off: 0x700, Vaddr: 0x400700, Paddr: 0x400700, Filesz: 0x500, Memsz: 0x710, Align: 0x200000},
				{Type: elf.PT_LOAD, Flags: elf.PF_R | elf.PF_W, Off: 0xe10, Vaddr: 0x600e10, Paddr: 0x600e10, Filesz: 0x230, Memsz: 0x238, Align: 0x200000},
			},
			wantLoadables: true,
		},
		{
			desc:          "medium file large mapping that includes all address space matches executable segment",
			file:          &testelf.MediumFile,
			pgoff:         0,
			memsz:         0xd6e000,
			wantHeaders:   []*elf.ProgHeader{{Type: elf.PT_LOAD, Flags: elf.PF_R | elf.PF_X, Off: 0, Vaddr: 0, Paddr: 0, Filesz: 0x51800, Memsz: 0x51800, Align: 0x200000}},
			wantLoadables: true,
		},
		{
			desc:          "large file match executable segment",
			file:          &testelf.LargeFile,
			pgoff:         0,
			memsz:         0x2ec5e000,
			wantHeaders:   []*elf.ProgHeader{{Type: elf.PT_LOAD, Flags: elf.PF_R | elf.PF_X, Off: 0, Vaddr: 0, Paddr: 0, Filesz: 0x2ec5d2c0, Memsz: 0x2ec5d2c0, Align: 0x200000}},
			wantLoadables: true,
		},
		{
			desc:          "large file match first data mapping",
			file:          &testelf.LargeFile,
			pgoff:         0x2ec5d000,
			memsz:         0x1362000,
			wantHeaders:   []*elf.ProgHeader{{Type: elf.PT_LOAD, Flags: elf.PF_R | elf.PF_W, Off: 0x2ec5d2c0, Vaddr: 0x2ee5d2c0, Paddr: 0x2ee5d2c0, Filesz: 0x1361118, Memsz: 0x1361150, Align: 0x200000}},
			wantLoadables: true,
		},
		{
			desc:          "large file, split mapping doesn't match",
			file:          &testelf.LargeFile,
			pgoff:         0x2ffbe000,
			memsz:         0xb11000,
			wantHeaders:   nil,
			wantLoadables: true,
		},
		{
			desc:          "large file, combined mapping matches second data mapping",
			file:          &testelf.LargeFile,
			pgoff:         0x2ffbe000,
			memsz:         0xb11000 + 0x181000,
			wantHeaders:   []*elf.ProgHeader{{Type: elf.PT_LOAD, Flags: elf.PF_R | elf.PF_W, Off: 0x2ffbe440, Vaddr: 0x303be440, Paddr: 0x303be440, Filesz: 0x4637c0, Memsz: 0xc91610, Align: 0x200000}},
			wantLoadables: true,
		},
	} {
		t.Run(tc.desc, func(t *testing.T) {
			gotHeaders, gotLoadables := ProgramHeadersForMapping(tc.file, tc.pgoff, tc.memsz)
			if !reflect.DeepEqual(gotHeaders, tc.wantHeaders) {
				t.Errorf("got program headers %q; want %q", buildList(gotHeaders), buildList(tc.wantHeaders))
			}
			if gotLoadables != tc.wantLoadables {
				t.Errorf("got loadable segments %v, want %v", gotLoadables, tc.wantLoadables)
			}
		})
	}
}
