package pprof

import (
	"github.com/google/pprof/profile"
)

func Fuzz(data []byte) int {
	profile.ParseData(data)
	return 0;
}
