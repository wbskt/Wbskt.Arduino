# Contributing to Wbskt.Arduino

How changes are written, committed and reviewed in this repo.

## Commit messages

Every commit subject, and every PR title, follows [Conventional Commits](https://www.conventionalcommits.org):

```
<type>(<scope>): <summary>

<body: why the change was needed, not a list of what changed>

Closes #N
```

- **type** is one of `feat`, `fix`, `perf`, `refactor`, `docs`, `test`, `chore`, `ci`, `build`.
- **scope** names the part of the code the change touches (client, ota, buffer, conformance, examples, ci).
  Leave it out, as in `docs: …`, when a change spans the repo.
- **summary** is lower case, in the imperative, with no full stop, and under about 72 characters:
  `fix(buffer): size the offline buffer in bytes`.
- One logical change per commit. Squash fix-ups before review rather than leaving "fix review" commits.
- Older history also has `(feat): …` and unprefixed subjects. Don't copy them.

PRs are merged by rebase, so each commit lands on `master` exactly as written. That is why the
commits, not only the PR title, follow the format.

## Branches and merging

- Never create a merge commit. Rebase onto `master` to catch up, and merge PRs with rebase (or squash).
- Never force-push a branch someone else owns or `master` itself.

## Pull requests

1. Find the GitHub issue(s) the change addresses and link them in the body (`Closes #N`, or `Refs #N`
   when it only does part of the work). Open an issue first if there is none.
2. Run the checks below before pushing.
3. **Review it again once it's open.** Read the full diff on GitHub as a fresh reviewer, not as the
   author: correctness and edge cases, concurrency and lock order, error handling, missing tests, and
   these conventions. Push fixes as follow-up commits, then add a line to the PR body:
   `Post-open review: <what was found and fixed, or "nothing found">`.
4. Wait for CI to pass, then ask for the merge.

## Checks before pushing

```bash
make -C test/host           # host unit tests (needs ArduinoJson cloned into test/host, see ci.yml)
make -C test/conformance    # the shared conformance suite against this client
pio ci examples/<name> --lib . --board esp32dev   # examples compile; full options in ci.yml
```

The conformance tests come from wbskt/Wbskt `master`, so a protocol change merges there first.
