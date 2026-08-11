import { defineConfig } from "vite";
import tailwindcss from "@tailwindcss/vite";

// The wasm module and its data are produced by CMake / served from disk, so the bundler must
// leave that import alone and resolve it at runtime.
export default defineConfig({
  plugins: [tailwindcss()],
  // SharedArrayBuffer needs a cross-origin-isolated page; the production host sets these too.
  server: {
    headers: {
      "Cross-Origin-Opener-Policy": "same-origin",
      "Cross-Origin-Embedder-Policy": "require-corp",
    },
  },
  build: {
    outDir: "dist",
    emptyOutDir: true,
    target: "es2022",
    rollupOptions: { external: ["/reVC.js"] },
  },
});
