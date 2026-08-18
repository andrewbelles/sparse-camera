# Working agreement

## Writing code

- Write code only when the relevant prompt says you may; otherwise return it in
  conversation for review.
- Write only the files named, and match the surrounding style.

## Running things

- Short checks are fine: compiles, `bash -n`, config error paths, `--check`
  modes, anything finishing in seconds.
- Long or full test targets need explicit permission. If a defect is suspected,
  say so and ask before running one to reproduce it.

## Git

- Read freely; `log`, `diff`, `blame`, and `show` beat guessing.
- Never write to history and never lock a worktree: no commit, push, merge,
  rebase, reset, tag, branch deletion, or force. Leave every branch available
  to check out.
- History carries the reasoning for why things are as they are.

## Comments and documentation

- No markup in source, configs, or shell: no backticks around identifiers, no
  bold, no prose em-dashes.
- State what is needed and nothing else. No restating the obvious, no
  narration, no anecdotes, no persuasion.
- Single-line comments in `scripts/`; C may use multi-line freely.
- Context and history belong in `README.md` or `docs/`. The exceptions in
  source are a specific pending change or a long-standing problem tied to a
  particular block of code.

## Structure

- The Makefile compiles and delegates; minimal targets, minimal arguments.
- Behavior comes from config files, not flags. Environment setup lives in one
  script.

## Reporting

- Separate genuine defects from expected behavior, and report failures plainly
  with their output.
