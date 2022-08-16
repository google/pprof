package report

import (
	"testing"
)

func TestPackageName(t *testing.T) {
	type testCase struct {
		name   string
		expect string
	}

	for _, c := range []testCase{
		// Unrecognized packages:
		{``, ``},
		{`name`, ``},
		{`[libjvm.so]`, ``},
		{`prefix/name/suffix`, ``},
		{`prefix(a.b.c,x.y.z)`, ``},
		{`<undefined>.a.b`, ``},
		{`(a.b)`, ``},

		// C++ symbols:
		{`Math.number`, `Math`},
		{`std::vector`, `std`},
		{`std::internal::vector`, `std`},

		// Java symbols:
		{`pkg.Class.name`, `pkg`},
		{`pkg.pkg.Class.name`, `pkg`},
		{`pkg.Class.name(a.b.c, x.y.z)`, `pkg`},
		{`pkg.pkg.Class.<init>`, `pkg`},
		{`pkg.pkg.Class.<init>(a.b.c, x.y.z)`, `pkg`},

		// Go symbols:
		{`pkg.name`, `pkg`},
		{`pkg.(*type).name`, `pkg`},
		{`path/pkg.name`, `path/pkg`},
		{`path/pkg.(*type).name`, `path/pkg`},
		{`path/path/pkg.name`, `path/path/pkg`},
		{`path/path/pkg.(*type).name`, `path/path/pkg`},
		{`some.url.com/path/pkg.fnID`, `some.url.com/path/pkg`},
		{`parent-dir/dir/google.golang.org/grpc/transport.NewFramer`, `parent-dir/dir/google.golang.org/grpc/transport`},
		{`parent-dir/dir/google.golang.org/grpc.(*Server).handleRawConn`, `parent-dir/dir/google.golang.org/grpc`},
	} {
		t.Run(c.name, func(t *testing.T) {
			if got := packageName(c.name); got != c.expect {
				t.Errorf("packageName(%q) = %#v, expecting %#v", c.name, got, c.expect)
			}
		})
	}
}
