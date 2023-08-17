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

if [ "$RUN_STATICCHECK" != "false" ]; then
  staticcheck ./...
fi

# Packages that have any tests.
PKG=$(go list -f '{{if .TestGoFiles}} {{.ImportPath}} {{end}}' ./...)

go test $PKG

for d in $PKG; do
  go test -race -coverprofile=profile.out -covermode=$MODE $d
  if [ -f profile.out ]; then
    cat profile.out | grep -v "^mode: " >> coverage.txt
    rm profile.out
  fi
done

go vet -all ./...
if [ "$RUN_GOLANGCI_LINTER" != "false" ];  then
  golangci-lint run -D errcheck --timeout=3m ./...  # TODO: Enable errcheck back.
fi

gofmt -s -d .
