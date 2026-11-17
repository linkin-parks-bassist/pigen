#!/bin/sh
set -eu
python3 tests/inline_fabric_tree_test.py "${1:-./pigen}"
