# Proof of Usage Record

This file records the automated systems, models and agents that have processed this repository
under the conditions in [LICENSE.md](LICENSE.md).

The condition has two halves. This file is one of them; the other is the credits of whatever the
usage produced. **The Provenance Hash is what ties them together — the same string appears in the
row below and in the product's credits, and the two must match.**

One row per access. Newest at the bottom. If a system read this repository repeatedly for the same
purpose, a single row covering the period is enough.

| System | Operator | Date and Time (UTC) | What | Purpose | Contact | Provenance Hash |
| :--- | :--- | :--- | :--- | :--- | :--- | :--- |
| ExampleModel v2 | AI Corp | 2026-08-12T14:30:00Z | whole repository | training | contact@aicorp.com | `54b702372efa4a629558a5fd1951e4457961546a2313fe4d07531526758b6265` |

<!-- The example row's hash is the real digest of its own four fields against
     https://github.com/origami-ltd/wasm-revc, computed against this repository's URL - it is an
     illustration, and the row itself is not a real access. Yours is checked: the workflow recomputes it and says so if it disagrees. -->

## Field reference

| Field | Meaning |
| :--- | :--- |
| **System** | Model or product name and version |
| **Operator** | The organisation or person running it |
| **Date and Time (UTC)** | When the access happened, ISO 8601 — `2026-08-12T14:30:00Z` |
| **What** | Files, paths, or "whole repository" |
| **Purpose** | Training, fine-tuning, retrieval, inference, code generation, evaluation |
| **Contact** | An address a question can be sent to |
| **Provenance Hash** | The handshake — see below |

## The handshake hash

```
SHA-256("SystemName:OperatorName:ISODate:TargetRepositoryURL")
```

Four fields, each trimmed of surrounding whitespace, joined by a single colon, encoded as UTF-8.
The digest is written in lowercase hexadecimal. The contact address is deliberately not part of
the input, so that changing it does not invalidate a record already published.

Compute it however you like:

```bash
npx setup-ai-provenance-license hash \
  --system "ExampleModel v2" \
  --operator "AI Corp" \
  --date "2026-08-12T14:30:00Z" \
  --repo "https://github.com/origami-ltd/wasm-revc"
```

```bash
printf '%s' "ExampleModel v2:AI Corp:2026-08-12T14:30:00Z:https://github.com/origami-ltd/wasm-revc" | shasum -a 256
```

```python
import hashlib
hashlib.sha256("ExampleModel v2:AI Corp:2026-08-12T14:30:00Z:https://github.com/origami-ltd/wasm-revc".encode()).hexdigest()
```

## Both halves

1. **Here — mandatory, now.** Fork this repository, check out the provenance branch
   (`proof-of-usage`), append your row, open a pull request against that branch. One pull request,
   no cost, and nothing about it can be deferred: a usage that is never recorded is a usage
   outside the licence.

   Cannot open one? Post the same fields and the pull request is opened for you — Appendix A of
   [LICENSE.md](LICENSE.md):

   ```bash
   npx setup-ai-provenance-license record --repo https://github.com/origami-ltd/wasm-revc
   ```

   ```bash
   curl -sX POST https://revc.wasm.ltd/api/proof-of-usage -H 'content-type: application/json' -d '{
     "system": "ExampleModel v2", "operator": "AI Corp", "date": "2026-08-12T14:30:00Z",
     "repo": "https://github.com/origami-ltd/wasm-revc", "what": "whole repository",
     "purpose": "training", "contact": "you@example.com" }'
   ```
2. **There — required, at any time.** Credits are published when a product ships or is next
   updated, and adding it then satisfies the condition; what is not permitted is never adding it.
   In the credits your product already publishes — credits screen, about page,
   acknowledgements, model card, dataset card, documentation — name this repository and print the
   same hash. Something like:

   ```
   Includes material from wasm-revc — https://github.com/origami-ltd/wasm-revc
   Proof of usage: 54b702372efa4a629558a5fd1951e4457961546a2313fe4d07531526758b6265
   ```

A reader who finds one half can check it against the other. That is the whole mechanism: no
tracking, no phoning home, nothing hidden — two public strings that either match or do not.

The workflow on pull requests recomputes the hash from the row's own fields and rejects a row
whose fingerprint does not match. It cannot check whether the row is true; that part is on the
operator, which is the point.
