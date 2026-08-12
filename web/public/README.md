# Engine binaries

`reVC.js` and `reVC.wasm` are build output, and they are committed here on purpose.

Vercel builds from git, and Vite copies this directory verbatim into `dist`. Without these files
in the repository every deploy that is not made by hand from a developer's machine — every
preview, every staging branch — renders the page and then reports that the engine is missing.
Building them in CI is not a realistic alternative: it needs Emscripten and a full engine compile.

Regenerate them with:

```bash
REVC_AUDIO=OAL ./scripts/build-web.sh
```

The two files are a matched set — the `.js` glue refers to addresses inside the `.wasm`, so a
mismatched pair aborts at startup with "No EM_ASM constant found at address N". Always commit
them together.
