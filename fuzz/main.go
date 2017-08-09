// Package pprof is used in conjunction with github.com/dvyukov/go-fuzz/go-fuzz
// to fuzz ParseData function.
package pprof

import (
	"github.com/google/pprof/profile"
)

// Fuzz can be used with https://github.com/dvyukov/go-fuzz to do fuzz testing on ParseData
func Fuzz(data []byte) int {
	profile.ParseData(data)
	return 0
}
