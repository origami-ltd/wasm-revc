# Security policy

## Reporting a vulnerability

**Do not open a public issue.** Use GitHub's private vulnerability reporting on the affected
repository — the *Security* tab, *Report a vulnerability* — or write to
**lbj.erasmo@gmail.com** with `SECURITY` in the subject.

Tell me what you found, how to reproduce it, and what an attacker gets out of it. A proof of
concept helps and is never required.

You will get a reply within a week. These are side projects maintained by one person; that is the
honest expectation to set rather than a service level nobody would meet.

## What is in scope

- The page at `https://revc.wasm.ltd` and its shell: the loader, the streaming layer, saves, the
  install picker.
- The build and deploy configuration in these repositories.
- Anything that lets a page read a visitor's files beyond the folder they picked, or exfiltrate
  data from their machine. That is the failure mode that matters most here: these pages ask for
  access to a game directory, and that trust is the whole product.

## What is out of scope

- Vulnerabilities in reVC itself (https://github.com/mrxenginner/reVC). A 2002 game engine
  compiled to WebAssembly is full of memory bugs by construction. What is in scope is a bug that escapes the WebAssembly sandbox or the browser's
  file access model — those, please report.
- Missing hardening headers that do not lead to an exploit, automated scanner output without a
  demonstrated impact, and reports about the game's own anti-cheat or piracy properties.
- Anything requiring the user to install a modified build of the software.

## Disclosure

Report privately, give me a chance to fix it, then publish whatever you like. I will credit you
unless you would rather I did not.
