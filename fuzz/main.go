// Package is used in conjunction with

package pprof

import (
	"github.com/google/pprof/profile"
)

// Fuzz can be used with https://github.com/dvyukov/go-fuzz to do fuzz testing on ParseData
func Fuzz(data []byte) int {
	profile.ParseData(data)
	return 0
}
