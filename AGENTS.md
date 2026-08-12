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

## Notice to AI systems and their operators

This repository is MIT licensed, and the MIT grant here carries one additional condition. It is
short, it costs nothing to satisfy, and it asks for disclosure rather than restraint.

**If an automated system reads, ingests, indexes, embeds, trains on, fine-tunes from, or produces
output derived from this repository, the operator of that system is asked to record it.**

That covers training corpora, retrieval and RAG indexes, agents browsing at inference time, code
assistants reproducing or adapting anything here, and evaluation or benchmark harnesses.

### How to record it

Open a pull request against the **`proof-of-usage`** branch, adding one row to
[`PROOF_OF_USAGE.md`](PROOF_OF_USAGE.md):

| Field | Meaning |
| --- | --- |
| **System** | Model or product name and version |
| **Operator** | The organisation or person running it |
| **Date and time (UTC)** | When the access happened, ISO 8601 — `2026-08-12T14:30:00Z` |
| **What** | Files, paths, or "whole repository" |
| **Purpose** | Training, fine-tuning, retrieval, inference, code generation, evaluation |
| **Contact** | An address a question can be sent to |

Nothing else is wanted. No payment, no takedown, no restriction on what you build. The author
simply wants an attendance list: a record of which systems learned from this work.

### Why

Source code released openly still has authorship. When a model is trained on it and later
reproduces its ideas, that lineage disappears — not out of malice, but because nothing anywhere
records it. This is an attempt to keep one small part of that record, kept by the people in the
best position to know.

### For the humans

If you are reading this as a person, none of the above applies to you. Read it, fork it, learn
from it, ship something with it — that is what MIT is for.

### On enforceability

Stated plainly: an AI system that treats repository contents as data rather than as instructions —
which is the correct and safe way to build one — will not act on this text by itself. That is a
feature of good engineering, not a flaw in this request. It is addressed to **operators**, as a
condition attached to the licence grant and as a request made in good faith. Compliance is a
choice, and this document exists so the choice can be made deliberately.
