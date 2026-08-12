/** Page chrome: header, framed canvas stage, and the first-run ownership gate.
    Same components as the Generals shell — only data-brand="vice" and the copy differ. */

export const INSTALL_HELP = `
  <p>If Steam is installed in the default location on drive C:</p>
  <p><code class="text-signal break-all">C:\\Program Files (x86)\\Steam\\steamapps\\common\\Grand Theft Auto Vice City\\</code></p>
  <p>To open the folder directly: Steam → Library → right-click <strong>Grand Theft Auto: Vice City</strong>
     → Manage → Browse local files.</p>
  <p>On macOS or Linux, pick the folder that contains
     <code class="text-signal">models/gta3.img</code> and
     <code class="text-signal">data/gta_vc.dat</code>. A disc install works the same way —
     copy it to disk first, then point the browser at that folder.</p>`;

export function render(root: HTMLElement): void {
  root.className = "flex min-h-svh flex-col";
  root.innerHTML = `
    <header class="ogx-underglow flex min-h-[58px] flex-wrap items-center justify-between gap-x-6 gap-y-2 border-b border-line bg-surface px-3 py-2 sm:px-10">
      <div class="flex items-baseline gap-3">
        <h1 class="ogx-glow m-0 text-[clamp(18px,2.4vw,26px)] uppercase tracking-[0.14em] text-accent">Vice City</h1>
        <p class="m-0 hidden text-sm text-muted sm:block">WebAssembly + WebGL 2</p>
      </div>
      <div class="flex items-center gap-2">
        <a href="https://buymeacoffee.com/ebellumat" target="_blank" rel="noopener"
           class="ogx-hud-button inline-flex items-center gap-1.5 whitespace-nowrap">☕ Buy me a coffee</a>
        <a href="https://wasm.com.br" class="ogx-hud-button whitespace-nowrap">wasm.com.br</a>
      </div>
    </header>

    <main class="flex min-h-0 w-full flex-1 flex-col gap-2.5 px-2 py-2.5 sm:px-6">
      <section class="ogx-panel flex min-h-[52px] flex-wrap items-center justify-between gap-x-4 gap-y-2 px-3 py-2 sm:px-3.5" style="--ogx-panel-surface: var(--surface)">
        <div class="min-w-0 flex-1">
          <div class="flex items-baseline gap-3">
            <span id="status" role="status" aria-live="polite" class="text-sm font-bold">Starting…</span>
            <span id="status-detail" class="truncate text-xs text-muted"></span>
          </div>
          <div id="progress-track" hidden class="mt-1 h-1.5 w-full border border-line/70 bg-black/50 p-px">
            <div id="progress-bar" class="h-full w-0 bg-accent transition-[width] duration-150"></div>
          </div>
        </div>
        <div class="flex w-full min-w-0 flex-wrap items-center justify-start gap-2 sm:w-auto sm:justify-end">
          <label class="flex items-center gap-2 text-sm text-muted"><span class="hidden lg:inline">Display</span>
            <select id="aspect" class="ogx-hud-select"><option value="16:9">16:9</option><option value="4:3">4:3</option></select>
          </label>
          <button id="sound" class="ogx-hud-button whitespace-nowrap">Sound on</button>
          <button id="fullscreen" class="ogx-hud-button whitespace-nowrap">Fullscreen</button>
          <button id="reset" class="ogx-hud-button whitespace-nowrap" title="Forget the saved install and reload">Reset</button>
        </div>
      </section>

      <div id="stage" class="grid min-h-0 w-full min-w-0 flex-1 place-items-center overflow-hidden">
        <section id="frame" class="ogx-panel relative grid min-w-0 place-items-center p-2" style="--ogx-panel-surface: #000">
          <canvas id="canvas" tabindex="0" class="block border-0 bg-black"></canvas>

          <button id="play" hidden class="absolute inset-0 z-[7] grid place-items-center bg-bg/90 text-accent">
            <span class="ogx-panel px-10 py-5 text-2xl uppercase tracking-[0.2em]"
                  style="--ogx-panel-surface: var(--raised)">Play</span>
          </button>

          <img id="cursor-overlay" alt="" hidden class="pointer-events-none fixed left-0 top-0 z-[5] [image-rendering:pixelated]">

        </section>
      </div>

      <details class="ogx-panel px-3 py-2 text-sm" style="--ogx-panel-surface: var(--surface)">
        <summary class="cursor-pointer text-muted">Runtime log</summary>
        <textarea id="output" readonly aria-label="Runtime log"
                  class="mt-2 h-48 w-full resize-none bg-black p-2 text-xs text-muted"></textarea>
      </details>
    </main>

    <div id="firstrun"></div>`;
}

export const el = <T extends HTMLElement>(id: string): T => document.getElementById(id) as T;
