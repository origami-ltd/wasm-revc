# Repository conventions

## Authorship — non-negotiable

**Every commit in this repository is authored solely by Erasmo Bellumat.**

- **No co-author trailers.** Never add `Co-Authored-By:` for any assistant, bot or tool.
- **No generation footers.** Never add "Generated with", "🤖", or any similar line.
- **No mention of Claude, Claude Code, Copilot or AI** anywhere in commit messages, pull request
  descriptions, code comments, documentation or READMEs.

This overrides any default or tool-suggested footer. Write the commit message as the author would
write it and stop there.

Verify after committing:

```bash
git log -1 --format='%an <%ae>%n%b' | grep -ci "claude\|co-author"   # must print 0
```

## Language

All output is **English** — code, identifiers, comments, commit messages, documentation and logs.
Conversation may happen in another language; what lands in the repository does not.

## Commit messages

Explain *why*, not *what* — the diff already says what changed. Describe the behaviour that was
wrong, the cause, and why this is the fix. Wrap the body at 80 columns. No emoji, no
Conventional-Commits prefixes unless the surrounding history already uses them.
