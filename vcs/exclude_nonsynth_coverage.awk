# Exclude firtool-emitted non-synthesis blocks from VCS code coverage while
# leaving their assertions and diagnostics compiled and active in simulation.

function start_nonsynth_block(line) {
  return line ~ /^[[:space:]]*`ifndef[[:space:]]+SYNTHESIS([[:space:]]|$)/
}

function nested_preprocessor_if(line) {
  return line ~ /^[[:space:]]*`(ifdef|ifndef)[[:space:]]+/
}

function preprocessor_endif(line) {
  return line ~ /^[[:space:]]*`endif([[:space:]]|$)/
}

function emit_block(    i, indent) {
  match(block[1], /^[[:space:]]*/)
  indent = substr(block[1], RSTART, RLENGTH)
  print indent "//VCS coverage off"
  for (i = 1; i <= block_lines; ++i)
    print block[i]
  print indent "//VCS coverage on"

  delete block
  block_lines = 0
}

{
  if (!in_nonsynth_block) {
    if (start_nonsynth_block($0)) {
      in_nonsynth_block = 1
      depth = 1
      block_lines = 1
      block[block_lines] = $0
      next
    }

    print
    next
  }

  block[++block_lines] = $0
  if (nested_preprocessor_if($0))
    ++depth
  if (preprocessor_endif($0))
    --depth

  if (depth == 0) {
    emit_block()
    in_nonsynth_block = 0
  }
}

END {
  if (in_nonsynth_block) {
    print "error: unterminated `ifndef SYNTHESIS block" > "/dev/stderr"
    exit 2
  }
}
