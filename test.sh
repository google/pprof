#  Copyright 2017 Google Inc. All Rights Reserved.
#
#  Licensed under the Apache License, Version 2.0 (the "License");
#  you may not use this file except in compliance with the License.
#  You may obtain a copy of the License at
#
#      http://www.apache.org/licenses/LICENSE-2.0
#
#  Unless required by applicable law or agreed to in writing, software
#  distributed under the License is distributed on an "AS IS" BASIS,
#  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
#  See the License for the specific language governing permissions and
#  limitations under the License.


#!/usr/bin/env bash

set -e
set -x
MODE=atomic
echo "mode: $MODE" > coverage.txt

# Note: browsertests is in a separate module and is therefore not
# covered by a local ./... pattern.

if [ "$RUN_STATICCHECK" != "false" ]; then
  staticcheck ./...
  (cd browsertests && staticcheck ./...)
fi

# Packages that have any tests.
PKG=$(go list -f '{{if .TestGoFiles}} {{.ImportPath}} {{end}}' ./...)

go test $PKG

retry() {
  for i in {1..3}; do
    [[ $i == 1 ]] || sleep 10  # Backing off after a failed attempt.
    "${@}" && return 0
  done
  return 1
}

# Retry browser tests in case of error since they are flaky.
# See https://github.com/google/pprof/issues/925.
(cd browsertests && retry go test ./...)
(cd browsertests && retry go test -race ./...)

# Skip browsertests since it test-only code and gives no useful coverage info
for d in $PKG; do
  go test -race -coverprofile=profile.out -covermode=$MODE $d
  if [ -f profile.out ]; then
    cat profile.out | grep -v "^mode: " >> coverage.txt
    rm profile.out
  fi
done

go vet -all ./...
(cd browsertests && go vet -all ./...)
if [ "$RUN_GOLANGCI_LINTER" != "false" ];  then
  golangci-lint run -D errcheck --timeout=3m ./...  # TODO: Enable errcheck back.
  (cd browsertests && golangci-lint run --timeout=3m ./...)
fi

# A workaround for gofmt returning zero exit code even if there is diff.
# See https://github.com/golang/go/issues/46289.
gofmt_or_fail() {
  test -z "$(gofmt -d -s . | tee >(cat >&2))"
}
gofmt_or_fail
(cd browsertests && gofmt_or_fail)
