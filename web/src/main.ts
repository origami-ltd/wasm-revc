import "./style.css";
import { ArchiveStreamer, allSupported, checkCapabilities, isHandheld } from "@wasm/runtime";
import { mountGate } from "@origami-ltd/ui/gate";
import {
  folders, findInstallRoot, GAME_ROOT, INSTALL_KEY, readInstall, readServedInstall, savedInstall,
  writeLooseFiles,
} from "./archives";
import type { Install } from "./archives";
import { el, INSTALL_HELP, render } from "./ui";
import type { EmscriptenModule, ModuleFactory } from "./types";

render(el("app"));

const canvas = el<HTMLCanvasElement>("canvas");
const frame = el("frame");
const stage = el("stage");
const output = el<HTMLTextAreaElement>("output");
const status = el("status");
const detail = el("status-detail");
const track = el("progress-track");
const bar = el("progress-bar");
const query = new URLSearchParams(location.search);

let module: EmscriptenModule | undefined;

/* ---------------------------------------------------------------- logging */
const shownLines: string[] = [];

/**
 * Everything the page knows, shipped to the host so it can be tailed from a terminal
 * (scripts/serve-web.py writes /tmp/vice-runtime.log). A browser console cannot be read from
 * outside the browser, and that is exactly where the useful symptom always shows up first —
 * engine debug output, uncaught wasm traps, and the stall detector below.
 */
const pending: string[] = [];
let logDead = false;

function ship(): void {
  if (!pending.length || logDead) return;
  const body = `${pending.join("\n")}\n`;
  pending.length = 0;
  void fetch("/ViceLog", { method: "POST", body }).catch(() => { logDead = true; });
}
setInterval(ship, 1000);
addEventListener("pagehide", ship);

function log(line: string): void {
  shownLines.push(line);
  if (shownLines.length > 512) shownLines.shift();
  pending.push(line);
  output.value = `${shownLines.join("\n")}\n`;
  output.scrollTop = output.scrollHeight;
}

// Uncaught traps are the ones that matter most and the ones the page would otherwise swallow.
addEventListener("error", (event) => log(`[js error] ${event.message} @ ${event.filename}:${event.lineno}`));
addEventListener("unhandledrejection", (event) => {
  const reason = event.reason as { message?: string; stack?: string } | undefined;
  log(`[unhandled] ${reason?.message ?? String(event.reason)}`);
  if (reason?.stack) log(reason.stack.split("\n").slice(0, 12).join("\n"));
});

/**
 * Stall detector. The engine's frame counter is the only honest liveness signal — the status
 * line says "Running" even when the loop has stopped dead, and a wasm trap inside the game loop
 * leaves a page that looks merely slow. Reports the first stall and the recovery, not every tick.
 */
let lastFrame = -1;
let lastProgress = Date.now();
let stalled = false;
setInterval(() => {
  const frame = module?._ViceLogicFrame?.();
  if (frame === undefined) return;
  if (frame !== lastFrame) {
    if (stalled) {
      log(`[stall] recovered after ${((Date.now() - lastProgress) / 1000).toFixed(1)}s at frame ${frame}`);
      stalled = false;
    }
    lastFrame = frame;
    lastProgress = Date.now();
    return;
  }
  const idle = Date.now() - lastProgress;
  if (idle > 5000 && !stalled) {
    stalled = true;
    log(`[stall] no frame for ${(idle / 1000).toFixed(1)}s at frame ${frame}`
      + ` — state=${module?._ViceGameState?.()} idle=${module?._ViceIdleCount?.()}`);
  }
}, 1000);

/** Everything the page is doing: headline, detail line, and a bar when there is a ratio. */
function report(headline: string, note = "", ratio?: number): void {
  if (headline) status.textContent = headline;
  detail.textContent = note;
  track.hidden = ratio === undefined;
  if (ratio !== undefined) bar.style.width = `${Math.round(Math.min(1, Math.max(0, ratio)) * 100)}%`;
}

const mb = (bytes: number): string => (bytes / 2 ** 20).toFixed(0);

/* ------------------------------------------------------------------ display */
// 16:9 is 1280x720, 4:3 is 1024x768. The engine reads this at startup (see psSelectDevice) so
// picking one costs a reload rather than a mid-session device reset.
const aspect = el<HTMLSelectElement>("aspect");
aspect.value = localStorage.getItem("vice.aspect") === "4:3" ? "4:3" : "16:9";
aspect.addEventListener("change", () => {
  localStorage.setItem("vice.aspect", aspect.value);
  location.reload();
});

/* ------------------------------------------------------------- letterboxing */
/**
 * The engine renders at a fixed 1280x720 and JS scales that to fit — the canvas never changes
 * its own size. Letting the render resolution chase the window meant a device reset on every
 * resize and a canvas that moved under the player; a fixed target is predictable, and the
 * engine's own resolution is the player's setting to change, not the layout's.
 */
function fitCanvas(): void {
  const fullscreen = document.fullscreenElement === frame;
  const availableWidth = Math.min(fullscreen ? innerWidth : stage.clientWidth, innerWidth) - 16;
  const availableHeight = Math.min(fullscreen ? innerHeight : stage.clientHeight, innerHeight) - 16;
  // Windowed is capped at 1 so the game is never a blurry upscale of itself. Fullscreen is the
  // deliberate "make it bigger" and must be allowed past that, or it just letterboxes 1280x720
  // in the middle of a black screen.
  const fit = Math.min(availableWidth / (canvas.width || 1), availableHeight / (canvas.height || 1));
  const scale = fullscreen ? fit : Math.min(fit, 1);
  canvas.style.width = `${Math.max(1, Math.floor((canvas.width || 1) * scale))}px`;
  canvas.style.height = `${Math.max(1, Math.floor((canvas.height || 1) * scale))}px`;
}

new ResizeObserver(fitCanvas).observe(stage);
new MutationObserver(fitCanvas).observe(canvas, { attributes: true, attributeFilter: ["width", "height"] });
addEventListener("resize", fitCanvas);
document.addEventListener("fullscreenchange", fitCanvas);

// Fullscreen the frame, not the canvas, so overlays stay inside the fullscreened subtree.
el("fullscreen").addEventListener("click", () => void frame.requestFullscreen().catch(() => {}));

/* ------------------------------------------------------------ mouse capture */
/**
 * FPS-style capture while playing.
 *
 * Unlocked, the pointer wanders off the canvas and the camera spins forever — the engine reads
 * motion that no longer has anything to do with the window. Locked, the OS pointer is pinned and
 * the game gets clean relative motion.
 *
 * ESC is the way out, and the browser takes it: pressing it exits pointer lock and never
 * delivers the key to the page. So the exit itself is the signal — when the lock drops while the
 * page still has focus, that WAS an ESC, and the game gets one synthesised so its own pause menu
 * opens. A lock lost to alt-tab or a hidden tab has no focus, so it does not fire.
 */
const GS_PLAYING_GAME = 9;
const inGameplay = () => module?._ViceGameState?.() === GS_PLAYING_GAME;

function capturePointer(): void {
  if (!inGameplay() || document.pointerLockElement) return;
  // requestPointerLock rejects rather than throws, and it legitimately fails in embedded or
  // unfocused documents ("root document of this element is not valid for pointer lock").
  // Unhandled, that noise drowns the runtime log; the game is still playable without capture.
  try {
    const request = canvas.requestPointerLock?.() as unknown as Promise<void> | undefined;
    if (request && typeof request.catch === "function") request.catch(() => {});
  } catch {
    /* older browsers throw synchronously */
  }
}

canvas.addEventListener("pointerdown", () => {
  canvas.focus();
  capturePointer();
});

document.addEventListener("pointerlockchange", () => {
  const locked = document.pointerLockElement === canvas;
  canvas.classList.toggle("ogx-pointer-locked", locked);
  if (locked || !document.hasFocus() || document.hidden || !inGameplay()) return;
  // The browser consumed the ESC that released the lock; hand it to the game.
  for (const type of ["keydown", "keyup"] as const) {
    canvas.dispatchEvent(new KeyboardEvent(type, { key: "Escape", code: "Escape", bubbles: true }));
  }
});

// Entering gameplay should capture without waiting for a stray click; the Play button already
// gave us the activation the browser requires.
let wasPlaying = false;
setInterval(() => {
  const playing = inGameplay();
  if (playing && !wasPlaying) capturePointer();
  if (!playing && document.pointerLockElement === canvas) document.exitPointerLock?.();
  wasPlaying = playing;
}, 400);

canvas.addEventListener("contextmenu", (event) => event.preventDefault());

/* ------------------------------------------------------------------- sound */
/**
 * Chrome starts an AudioContext suspended unless it was created during a user gesture, and
 * emscripten's OpenAL creates its context when the engine initialises its audio — well after the
 * Play click. A suspended context is not an error and reports no failure: the game simply runs
 * silent. So resume anything suspended, on every gesture and whenever the tab comes back, and
 * keep doing it — the engine opens devices lazily, long after startup.
 */
function resumeAudio(): void {
  const contexts = (window as unknown as { __viceAudioContexts?: AudioContext[] }).__viceAudioContexts;
  for (const context of contexts ?? []) {
    if (context.state === "suspended" && !document.hidden) void context.resume().catch(() => {});
  }
}

// Catch every AudioContext the engine creates, so they can be resumed later.
{
  const Original = window.AudioContext ?? (window as unknown as { webkitAudioContext: typeof AudioContext }).webkitAudioContext;
  const created: AudioContext[] = [];
  (window as unknown as { __viceAudioContexts: AudioContext[] }).__viceAudioContexts = created;
  const Patched = function (this: unknown, ...args: unknown[]) {
    const context = new (Original as unknown as new (...a: unknown[]) => AudioContext)(...args);
    created.push(context);
    return context;
  } as unknown as typeof AudioContext;
  Patched.prototype = Original.prototype;
  window.AudioContext = Patched;
  (window as unknown as { webkitAudioContext: typeof AudioContext }).webkitAudioContext = Patched;
}

for (const type of ["pointerdown", "keydown", "visibilitychange"]) {
  addEventListener(type, resumeAudio, { capture: true });
}
setInterval(resumeAudio, 1000);

let soundMuted = localStorage.getItem("vice.soundMuted") === "1";

function setSoundMuted(muted: boolean): void {
  soundMuted = muted;
  localStorage.setItem("vice.soundMuted", muted ? "1" : "0");
  module?._ViceSetAudioMuted?.(muted ? 1 : 0);
  el("sound").textContent = muted ? "Sound off" : "Sound on";
}
el("sound").addEventListener("click", () => setSoundMuted(!soundMuted));
el("sound").textContent = soundMuted ? "Sound off" : "Sound on";

el("reset").addEventListener("click", () => {
  localStorage.clear();
  folders.clear();
  location.reload();
});

/* ------------------------------------------------------------ first-run gate */
const capabilities = checkCapabilities("webgl2");

const gate = mountGate(el("firstrun"), {
  game: "Grand Theft Auto: Vice City",
  help: INSTALL_HELP,
  capabilities,
  handheld: isHandheld(),
  pickerId: "wasm-vice-city-install",
  onPick: async (picked) => {
    const root = await findInstallRoot(picked);
    if (!root) return "No Vice City install under that folder — it needs models/gta3.img and data/gta_vc.dat.";
    await folders.save(new Map([[INSTALL_KEY, root]]));
    setTimeout(() => location.replace(location.pathname), 700);
    return undefined;
  },
});

if (gate.blocked) {
  gate.show();
  report("Unsupported browser", "see what this page needs");
}

/* -------------------------------------------------------------------- boot */
const streamer = new ArchiveStreamer(log);

/** Mount the install: the big archives stream, the small files are copied into MEMFS. */
async function mountInstall(instance: EmscriptenModule, install: Install): Promise<void> {
  for (const entry of install.streamed) streamer.mount(instance, entry);
  log(`Mounted ${install.streamed.length} archives.`);

  // One progress indicator, the bar in the status strip. A second one over the canvas said the
  // same thing twice.
  await writeLooseFiles(instance, install.loose, (done, total, path) => {
    report("Loading game files", `${path} · ${done}/${total}`, done / total);
  });

  // Pull the big archives through the network into the browser's own disk cache before the game
  // opens. The engine reads them synchronously — the main thread spin-waits on every read — so a
  // cold cache turns world streaming into thousands of blocking round trips and a frame can take
  // seconds. Primed, those same reads are local. Nothing is retained in JS: prime reads and drops.
  const streamedBytes = install.streamed.reduce((sum, entry) => sum + entry.size, 0);
  await streamer.prime(install.streamed, (done, name) => {
    report("Caching archives", `${name} · ${mb(done)}/${mb(streamedBytes)} MB`, done / streamedBytes);
  });
  log("Archives cached locally; reads no longer touch the network.");
  report("", "");
  log(`Loaded ${install.loose.length} loose game files into memory.`);
}

const config: Record<string, unknown> = {
  canvas,
  arguments: [],
  print: (line: string) => log(line),
  printErr: (line: string) => log(line),
  preRun: [
    (instance: EmscriptenModule) => {
      instance.addRunDependency("vice-assets");
      void (async () => {
        // ?install=server takes the install from the host instead of a picked folder
        // (scripts/serve-web.py). Same code path from here on — the engine cannot tell.
        if (query.get("install") === "server") {
          await streamer.ready;
          instance.FS.mkdirTree(GAME_ROOT);
          await mountInstall(instance, await readServedInstall());
          instance.FS.chdir(GAME_ROOT);
          instance.removeRunDependency("vice-assets");
          return;
        }

        const root = await savedInstall();
        if (!root) {
          // ?engine=1 boots with an empty game directory. The engine cannot get far without
          // assets, but it proves the wasm module, SDL, WebGL and the Asyncify yield all come
          // up — which is the only thing that can be tested before a full install exists.
          if (query.get("engine") === "1") {
            instance.FS.mkdirTree(GAME_ROOT);
            instance.FS.chdir(GAME_ROOT);
            log("Engine smoke test: booting with no game files.");
            instance.removeRunDependency("vice-assets");
            return;
          }
          gate.show();
          log("No install selected yet — waiting for the player to point at their copy.");
          return; // dependency stays: no game files, no game
        }
        await streamer.ready;
        instance.FS.mkdirTree(GAME_ROOT);
        await mountInstall(instance, await readInstall(root));
        // The engine opens everything by relative path ("models/gta3.img", "DATA/GTA_VC.DAT"),
        // so the install has to be the working directory.
        instance.FS.chdir(GAME_ROOT);
        instance.removeRunDependency("vice-assets");
      })().catch((error: Error) => {
        log(`Could not read the install: ${error.message}`);
        gate.show();
      });
    },
  ],
};

// main() never returns (it hands control to the browser main loop), so the factory promise never
// settles — readiness comes from onRuntimeInitialized instead of awaiting it.
config.onRuntimeInitialized = function (this: EmscriptenModule) {
  module = this;
  (globalThis as unknown as { __vice: EmscriptenModule }).__vice = this;
  frame.dataset.ready = "true";
  report("Running", "");
  fitCanvas();
  if (soundMuted) module._ViceSetAudioMuted?.(1);
};

// ?assets=1 means the player came to repoint their install.
if (query.get("assets") === "1") gate.show();

// The emscripten build is produced by CMake (scripts/build-web.sh) and served beside this page.
// It has to stay invisible to the bundler: Vite rewrites even a @vite-ignore'd dynamic import of
// a public/ path, and the engine is not something to bundle anyway. An indirect import through
// Function() is a plain runtime fetch that Vite cannot see.
// Absence is a normal state — the shell still runs so the install gate works before a build exists.
const ENGINE_URL = "/reVC.js";
const importEngine = new Function("url", "return import(url)") as (url: string) => Promise<unknown>;
const factory = allSupported(capabilities)
  ? await importEngine(ENGINE_URL)
      .then((loaded) => (loaded as { default: ModuleFactory }).default)
      .catch(() => undefined)
  : undefined;

if (gate.blocked) {
  // Nothing more to do: the gate is up and explains which requirement is missing.
} else if (!factory) {
  report("Engine not built yet", "the reVC WebAssembly build is not on this host");
  log("No /reVC.js on this host — build the emscripten target and serve it beside this page.");
  if (!(await savedInstall())) gate.show();
} else {
  const play = el<HTMLButtonElement>("play");
  play.hidden = false;
  report("Ready", "Welcome back to the 80s");
  // Chrome refuses to start an AudioContext without user activation, and the engine creates its
  // device during init — so the runtime only starts once the player has clicked Play.
  play.addEventListener("click", () => {
    play.hidden = true;
    report("Starting engine", "loading game data");
    void factory(config);
  }, { once: true });
}
