---
description: Commit staged/unstaged changes with a drafted message and push the current branch
---

Commit the current changes and push them.

1. Run in parallel: `git status` (never `-uall`), `git diff` (staged + unstaged),
   `git log --oneline -10` (for this repo's message style), and check whether
   the current branch has an upstream (`git rev-parse --abbrev-ref --symbolic-full-name @{u}`).

2. Stage relevant untracked files by name (never `git add -A`/`.`). Skip
   anything that looks like a secret (`.env`, credentials) and warn if asked
   to commit one anyway.

3. Draft a concise commit message (1-2 sentences, why over what) matching the
   repo's existing log style, and commit via heredoc ending with:
   ```
   Co-Authored-By: Claude Sonnet 5 <noreply@anthropic.com>
   ```
   If there is nothing staged and nothing untracked worth adding, stop and
   report that there's nothing to commit rather than creating an empty commit.

4. Push the branch:
   - If it already tracks a remote, `git push`.
   - If not, `git push -u origin HEAD`.
   - Never force-push. If a normal push is rejected (non-fast-forward), stop
     and report it rather than force-pushing.

5. Report the commit hash/message and confirm the push succeeded (or explain
   why it didn't).
