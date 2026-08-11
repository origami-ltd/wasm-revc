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
        <p class="m-0 hidden text-sm text-muted sm:block">WebAssembly + WebGPU</p>
      </div>
      <div class="flex items-center gap-2">
        <a href="https://buymeacoffee.com/ebellumat" target="_blank" rel="noopener"
           class="ogx-hud-button inline-flex items-center gap-1.5 whitespace-nowrap">☕ Buy me a coffee</a>
        <a href="https://wasm.com.br" class="ogx-hud-button whitespace-nowrap">wasm.com.br</a>
      </div>
    </header>

    <main class="flex min-h-0 w-full flex-1 flex-col gap-2.5 px-2 py-2.5 sm:px-6">
      <section class="ogx-panel flex min-h-[52px] flex-wrap items-center justify-between gap-2 px-3.5 py-2" style="--ogx-panel-surface: var(--surface)">
        <div class="min-w-0 flex-1">
          <div class="flex items-baseline gap-3">
            <span id="status" role="status" aria-live="polite" class="text-sm font-bold">Starting…</span>
            <span id="status-detail" class="truncate text-xs text-muted"></span>
          </div>
          <div id="progress-track" hidden class="mt-1 h-1.5 w-full border border-line/70 bg-black/50 p-px">
            <div id="progress-bar" class="h-full w-0 bg-accent transition-[width] duration-150"></div>
          </div>
        </div>
        <div class="flex min-w-0 flex-wrap items-center justify-end gap-2">
          <span id="cap-wasm" hidden class="border-l-[3px] border-signal bg-raised px-2 py-1 text-xs text-signal">WASM missing</span>
          <span id="cap-webgpu" hidden class="border-l-[3px] border-signal bg-raised px-2 py-1 text-xs text-signal">WebGPU missing</span>
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

          <!-- Loading the install: one ring, one number, one line. -->
          <div id="holo" hidden class="absolute inset-0 z-[8] grid place-items-center bg-bg/97">
            <div class="grid justify-items-center gap-4">
              <div class="ogx-ring-wrap">
                <svg class="ogx-ring" viewBox="0 0 120 120" aria-hidden="true">
                  <circle class="ogx-ring-track" cx="60" cy="60" r="54"></circle>
                  <circle id="holo-ring-fill" class="ogx-ring-fill" cx="60" cy="60" r="54"></circle>
                </svg>
                <div class="ogx-ring-center">
                  <div id="holo-percent" class="ogx-ring-percent">0%</div>
                  <div id="holo-mb" class="ogx-ring-note mt-1">reading your install…</div>
                </div>
              </div>
              <div id="holo-file" class="ogx-ring-file">&nbsp;</div>
            </div>
          </div>

          <img id="cursor-overlay" alt="" hidden class="pointer-events-none fixed left-0 top-0 z-[5] [image-rendering:pixelated]">

          <div id="firstrun" hidden class="absolute inset-0 z-[6] grid place-items-center bg-bg/94 p-4">
            <div class="ogx-panel max-h-full max-w-4xl overflow-auto p-4 text-left sm:p-7" style="--ogx-panel-surface: var(--raised)">
              <h2 class="ogx-glow m-0 mb-2 uppercase tracking-[0.12em] text-accent">Load your game files</h2>
              <p class="mb-5 text-[13px] text-muted">This page runs your own copy of
                 <strong>Grand Theft Auto: Vice City</strong>. Nothing is downloaded and nothing is
                 redistributed — the files never leave your machine.</p>
              <div class="border border-line bg-surface p-4">
                <h3 class="m-0 mb-2 flex items-center gap-2 text-sm uppercase tracking-[0.08em] text-accent">
                  Select your game folder
                  <button id="firstrun-info" aria-label="Where to find the game folder"
                          class="ogx-hud-button h-5 min-h-5 w-5 rounded-full px-0 text-xs [clip-path:none]">i</button>
                </h3>
                <p class="text-[13px] text-muted">Point the browser at your installed copy. The files stay on your machine.</p>
                <button id="firstrun-folder" class="ogx-hud-button mt-2">Select game folder</button>
                <p id="firstrun-folder-note" class="min-h-4 text-xs text-signal"></p>
              </div>
              <div id="firstrun-info-panel" hidden class="mt-4 space-y-2 border-l-[3px] border-accent bg-surface p-3.5 text-xs text-muted">${INSTALL_HELP}</div>
            </div>
          </div>
        </section>
      </div>

      <details class="ogx-panel px-3 py-2 text-sm" style="--ogx-panel-surface: var(--surface)">
        <summary class="cursor-pointer text-muted">Runtime log</summary>
        <textarea id="output" readonly aria-label="Runtime log"
                  class="mt-2 h-48 w-full resize-none bg-black p-2 text-xs text-muted"></textarea>
      </details>
    </main>`;
}

export const el = <T extends HTMLElement>(id: string): T => document.getElementById(id) as T;
