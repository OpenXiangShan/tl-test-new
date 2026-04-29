#!/bin/bash
RUN_DIR=${1:-./run}
cd "$RUN_DIR" && ./tltest_v3lt 2>&1 | tee tltest_v3lt.log
exit ${PIPESTATUS[0]}

# NOTICE:
# * Wrap executable with piped tee to save log.
# * Keep the logging procedure simple for now, sophisticated implementation
#   might be available in future.
#
