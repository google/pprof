#  Copyright 2021 Google Inc. All Rights Reserved.
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

set -eu
set -o pipefail

D3FLAMEGRAPH_CSS="d3-flamegraph.css"

cd $(dirname $0)

install_and_copy() {
    npm install

    cp node_modules/d3-flame-graph/dist/${D3FLAMEGRAPH_CSS} d3.css
}

get_licenses() {
    cp node_modules/d3-selection/LICENSE D3_LICENSE
    cp node_modules/d3-flame-graph/LICENSE D3_FLAME_GRAPH_LICENSE
}

get_licenses
install_and_copy
