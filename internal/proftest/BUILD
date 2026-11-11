# Description:
#   Auto-imported from github.com/google/pprof/internal/proftest

licenses(["notice"])

package(
    default_applicable_licenses = ["//third_party/golang/pprof:license"],
    default_visibility = ["//third_party/golang/pprof/internal:friends"],
)

go_library(
    name = "proftest",
    srcs = ["proftest.go"],
    embedsrcs = ["testdata/large.cpu"],
)

go_test(
    name = "proftest_test",
    srcs = ["proftest_test.go"],
    embed = [":proftest"],
)
