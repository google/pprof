// Package testelf provides elf.File objects for testing.
package testelf

import "debug/elf"

// Variuos ELF File definitions for unit tests.
var (
	TinyFile = elf.File{
		Progs: []*elf.Prog{
			{ProgHeader: elf.ProgHeader{Type: elf.PT_LOAD, Flags: elf.PF_R | elf.PF_X, Off: 0, Vaddr: 0, Paddr: 0, Filesz: 0xc80, Memsz: 0xc80, Align: 0x200000}},
			{ProgHeader: elf.ProgHeader{Type: elf.PT_LOAD, Flags: elf.PF_R | elf.PF_W, Off: 0xc80, Vaddr: 0x200c80, Paddr: 0x200c80, Filesz: 0x1f0, Memsz: 0x1f0, Align: 0x200000}},
		},
	}

	TinyBadBSSFile = elf.File{
		Progs: []*elf.Prog{
			{ProgHeader: elf.ProgHeader{Type: elf.PT_LOAD, Flags: elf.PF_R | elf.PF_X, Off: 0, Vaddr: 0, Paddr: 0, Filesz: 0xc80, Memsz: 0xc80, Align: 0x200000}},
			{ProgHeader: elf.ProgHeader{Type: elf.PT_LOAD, Flags: elf.PF_R | elf.PF_W, Off: 0xc80, Vaddr: 0x200c80, Paddr: 0x200c80, Filesz: 0x100, Memsz: 0x1f0, Align: 0x200000}},
			{ProgHeader: elf.ProgHeader{Type: elf.PT_LOAD, Flags: elf.PF_R | elf.PF_W, Off: 0xd80, Vaddr: 0x400d80, Paddr: 0x400d80, Filesz: 0x90, Memsz: 0x90, Align: 0x200000}},
		},
	}

	SmallFile = elf.File{
		Progs: []*elf.Prog{
			{ProgHeader: elf.ProgHeader{Type: elf.PT_PHDR, Flags: elf.PF_R | elf.PF_X, Off: 0x40, Vaddr: 0x400040, Paddr: 0x400040, Filesz: 0x1f8, Memsz: 0x1f8, Align: 8}},
			{ProgHeader: elf.ProgHeader{Type: elf.PT_INTERP, Flags: elf.PF_R, Off: 0x238, Vaddr: 0x400238, Paddr: 0x400238, Filesz: 0x1c, Memsz: 0x1c, Align: 1}},
			{ProgHeader: elf.ProgHeader{Type: elf.PT_LOAD, Flags: elf.PF_R | elf.PF_X, Off: 0, Vaddr: 0x400000, Paddr: 0x400000, Filesz: 0x6fc, Memsz: 0x6fc, Align: 0x200000}},
			{ProgHeader: elf.ProgHeader{Type: elf.PT_LOAD, Flags: elf.PF_R | elf.PF_W, Off: 0xe10, Vaddr: 0x600e10, Paddr: 0x600e10, Filesz: 0x230, Memsz: 0x238, Align: 0x200000}},
			{ProgHeader: elf.ProgHeader{Type: elf.PT_DYNAMIC, Flags: elf.PF_R | elf.PF_W, Off: 0xe28, Vaddr: 0x600e28, Paddr: 0x600e28, Filesz: 0x1d0, Memsz: 0x1d0, Align: 8}},
		},
	}

	SmallBadBSSFile = elf.File{
		Progs: []*elf.Prog{
			{ProgHeader: elf.ProgHeader{Type: elf.PT_LOAD, Flags: elf.PF_R | elf.PF_X, Off: 0, Vaddr: 0x200000, Paddr: 0x200000, Filesz: 0x6fc, Memsz: 0x6fc, Align: 0x200000}},
			{ProgHeader: elf.ProgHeader{Type: elf.PT_LOAD, Flags: elf.PF_R | elf.PF_W, Off: 0x700, Vaddr: 0x400700, Paddr: 0x400700, Filesz: 0x500, Memsz: 0x710, Align: 0x200000}},
			{ProgHeader: elf.ProgHeader{Type: elf.PT_LOAD, Flags: elf.PF_R | elf.PF_W, Off: 0xe10, Vaddr: 0x600e10, Paddr: 0x600e10, Filesz: 0x230, Memsz: 0x238, Align: 0x200000}},
		},
	}

	MediumFile = elf.File{
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

	LargeFile = elf.File{
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
)
