#!/bin/sh

COMMIT_TYPE_PATTERN='feat|fix|perf|refactor|test|docs|build|ci|chore'
COMMIT_TYPE_LIST='feat fix perf refactor test docs build ci chore'
COMMIT_MSG_REGEX="^(${COMMIT_TYPE_PATTERN})(\\([a-zA-Z0-9_-]+\\))?: .{5,}$"