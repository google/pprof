// Copyright 2019 Google Inc. All Rights Reserved.
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

// This is a script that generates the test executables for MacOS and Linux
// in this directory. It should be needed very rarely to run this script.
// It is mostly provided as a future reference on how the original binary
// set was created.

// When a new executable is generated, hardcoded addresses in the
// functions TestObjFile, TestMachoFiles, TestPEFile in binutils_test.go must be updated.
package main

import (
	"log"
	"os"
	"os/exec"
	"path/filepath"
	"runtime"
)

func main() {
	wd, err := os.Getwd()
	if err != nil {
		log.Fatal(err)
	}

	switch runtime.GOOS {
	case "linux":
		if err := removeGlob("exe_linux_64*"); err != nil {
			log.Fatal(err)
		}

		out, err := exec.Command("cc", "-g", "-ffile-prefix-map="+wd+"="+"/tmp", "-o", "exe_linux_64", "hello.c").CombinedOutput()
		log.Println(string(out))
		if err != nil {
			log.Fatal(err)
		}

	case "darwin":
		if err := removeGlob("exe_mac_64*", "lib_mac_64"); err != nil {
			log.Fatal(err)
		}

		out, err := exec.Command("clang", "-g", "-ffile-prefix-map="+wd+"="+"/tmp", "-o", "exe_mac_64", "hello.c").CombinedOutput()
		log.Println(string(out))
		if err != nil {
			log.Fatal(err)
		}

		out, err = exec.Command("clang", "-g", "-ffile-prefix-map="+wd+"="+"/tmp", "-o", "lib_mac_64", "-dynamiclib", "lib.c").CombinedOutput()
		log.Println(string(out))
		if err != nil {
			log.Fatal(err)
		}

	case "windows":
		// Many gcc enviroments may create binaries that trigger false-positives
		// in antiviruses. MSYS2 with gcc 10.2.0 is a working environment for
		// compiling. To setup the environment follow the guide at
		// https://www.msys2.org/ and install gcc with `pacman -S gcc`.
		out, err := exec.Command("gcc", "-g", "-ffile-prefix-map="+wd+"=", "-o", "exe_windows_64.exe", "hello.c").CombinedOutput()
		log.Println(string(out))
		if err != nil {
			log.Fatal(err)
		}
		log.Println("Please verify that exe_windows_64.exe does not trigger any antivirus on `virustotal.com`.")
	default:
		log.Fatalf("Unsupported OS %q", runtime.GOOS)
	}
}

func removeGlob(globs ...string) error {
	for _, glob := range globs {
		matches, err := filepath.Glob(glob)
		if err != nil {
			return err
		}
		for _, p := range matches {
			os.Remove(p)
		}
	}
	return nil
}
