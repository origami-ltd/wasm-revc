import "./style.css";
import { ArchiveStreamer, allSupported } from "@wasm/runtime";
import { createShell, el, hostInstall, mb, query } from "@wasm/shell";
import {
  folders, findInstallRoot, GAME_ROOT, INSTALL_KEY, readInstall, readServedInstall, savedInstall,
  writeLooseFiles,
} from "./archives";
import type { Install } from "./archives";
import { INSTALL_HELP } from "./ui";
import type { EmscriptenModule, ModuleFactory } from "./types";

let module: EmscriptenModule | undefined;


/** Gameplay, as opposed to the frontend menus — the state that wants the pointer captured. */
const GS_PLAYING_GAME = 9;
const inGameplay = (): boolean => module?._ViceGameState?.() === GS_PLAYING_GAME;

const shell = createShell({
  key: "vice",
  title: "Vice City",
  subtitle: "WebAssembly + WebGL 2",
  game: "Grand Theft Auto: Vice City",
  gpu: "webgl2",
  heapBytes: 1024 ** 3, // -sINITIAL_MEMORY=1073741824 in src/CMakeLists.txt
  help: INSTALL_HELP,
  repo: "origami-ltd/wasm-vice-city",
  logEndpoint: "/ViceLog",
  frame: () => module?._ViceLogicFrame?.(),
  applyMute: (muted) => module?._ViceSetAudioMuted?.(muted ? 1 : 0),
  setResolution: (w, h) => module?._ViceSetResolution?.(w, h),
  pointer: {
    // Menus need the free pointer to drive their own cursor; gameplay is FPS-style capture.
    wantsCapture: inGameplay,
    ready: () => el("frame").dataset.ready === "true",
  },
  assetDependency: "vice-assets",
  resumeSaved: () => savedInstall({ request: true }),
  mountPicked: async (instance, root) => {
    await streamer.ready;
    instance.FS.mkdirTree(GAME_ROOT);
    await mountInstall(instance, await readInstall(root));
    await mountSaves(instance);
    instance.FS.chdir(GAME_ROOT);
  },
  onReset: () => folders.clear(),
  onPick: async (picked) => {
    const root = await findInstallRoot(picked);
    if (!root) return "No Vice City install under that folder — it needs models/gta3.img and data/gta_vc.dat.";
    await folders.save(new Map([[INSTALL_KEY, root]]));
    return undefined;
  },
});

const { log, status, gate } = shell;
const canvas = el<HTMLCanvasElement>("canvas");

/* ------------------------------------------------------------------ display */
/**
 * Aspect only. The resolution itself belongs to Options -> Display inside the game, which can
 * pick anything in its list — the canvas follows it and the page scales the result to fit, so
 * rendering at 1920x1080 in a smaller canvas is a normal thing to ask for.
 *
 * This picks which resolution the game *starts* at the first time, nothing more: 16:9 boots at
 * 1280x720, 4:3 at 1024x768. Changing it clears the saved mode so the new default takes hold.
 */
const aspect = el<HTMLSelectElement>("aspect");
aspect.value = localStorage.getItem("vice.aspect") === "4:3" ? "4:3" : "16:9";
aspect.addEventListener("change", () => {
  localStorage.setItem("vice.aspect", aspect.value);
  localStorage.removeItem("vice.mode");
  location.reload();
});

/* -------------------------------------------------------------------- boot */
const streamer = new ArchiveStreamer(log);

/**
 * Make the save directory durable.
 *
 * The engine writes saves, gta_vc.set and the stats file into `userfiles` under the install root.
 * Everything else in the FS is MEMFS and dies with the tab, which is fine for game data the
 * player already has on disk — but not for their progress. IDBFS keeps a copy in IndexedDB;
 * syncfs(true) reads it back at boot, and the engine calls ViceSyncSaves() after each write to
 * push it out again.
 */
async function mountSaves(instance: EmscriptenModule): Promise<void> {
  const path = `${GAME_ROOT}/userfiles`;
  try {
    instance.FS.mkdirTree(path);
    const FS = instance.FS as unknown as {
      mount(fs: unknown, opts: object, path: string): void;
      filesystems: { IDBFS?: unknown };
      syncfs(populate: boolean, cb: (err?: Error) => void): void;
    };
    if (!FS.filesystems.IDBFS) {
      log("No IDBFS in this build — saves will not survive a reload.");
      return;
    }
    FS.mount(FS.filesystems.IDBFS, {}, path);
    // populate: pull whatever IndexedDB already holds into the in-memory tree.
    await new Promise<void>((resolve) => {
      FS.syncfs(true, (err) => {
        if (err) log(`Could not read saved games: ${err.message}`);
        resolve();
      });
    });
  } catch (error) {
    log(`Saves are not persistent: ${(error as Error).message}`);
  }
}

/** Mount the install: the big archives stream, the small files are copied into MEMFS. */
async function mountInstall(instance: EmscriptenModule, install: Install): Promise<void> {
  for (const entry of install.streamed) streamer.mount(instance, entry);
  const streamedBytes = install.streamed.reduce((sum, entry) => sum + entry.size, 0);
  log(`Streaming ${install.streamed.length} archives (${mb(streamedBytes)} MB) on demand.`);
  // Name the file being written, the way the other port names each archive — a bare count says
  // nothing about whether a long load is progressing or wedged.
  await writeLooseFiles(instance, install.loose, (done, total, path) => {
    status.report("Loading game files", `${path} · ${done}/${total}`, done / total);
  });
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
      shell.holdEngine(instance);
      void (async () => {
        // A dev host serves the install itself and publishes /ViceAssets; a static host does not.
        // Asking costs one request and means the page needs no flag to find what is already there.
        const served = await hostInstall("/ViceAssets", readServedInstall, log);
        const root = served ? undefined : await savedInstall();

        if (!served && !root) {
          // ?engine=1 boots with an empty game directory. The engine cannot get far without
          // assets, but it proves the wasm module, SDL, WebGL and the Asyncify yield all come
          // up — which is the only thing that can be tested before a full install exists.
          if (query.engineOnly) {
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
        await mountInstall(instance, served ?? await readInstall(root as FileSystemDirectoryHandle));
        await mountSaves(instance);
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
  el("frame").dataset.ready = "true";
  status.report("Running", "");
  shell.fit();
  shell.sound.sync();
};

// ?assets=1 means the player came to repoint their install.
if (query.pickInstall) gate.show();

// The emscripten build is produced by CMake (scripts/build-web.sh) and served beside this page.
// It has to stay invisible to the bundler: Vite rewrites even a @vite-ignore'd dynamic import of
// a public/ path, and the engine is not something to bundle anyway. An indirect import through
// Function() is a plain runtime fetch that Vite cannot see.
// Absence is a normal state — the shell still runs so the install gate works before a build exists.
const ENGINE_URL = "/reVC.js";
const importEngine = new Function("url", "return import(url)") as (url: string) => Promise<unknown>;
const factory = allSupported(shell.capabilities)
  ? await importEngine(ENGINE_URL)
      .then((loaded) => (loaded as { default: ModuleFactory }).default)
      .catch(() => undefined)
  : undefined;

if (gate.blocked) {
  // Nothing more to do: the gate is up and explains which requirement is missing.
} else if (!factory) {
  status.report("Engine not built yet", "the reVC WebAssembly build is not on this host");
  log("No /reVC.js on this host — build the emscripten target and serve it beside this page.");
  if (!(await savedInstall())) gate.show();
} else {
  const play = el<HTMLButtonElement>("play");
  play.hidden = false;
  status.report("Ready", "Welcome back to the 80s");
  // Chrome refuses to start an AudioContext without user activation, and the engine creates its
  // device during init — so the runtime only starts once the player has clicked Play.
  play.addEventListener("click", () => {
    play.hidden = true;
    status.report("Starting engine", "loading game data");
    void factory(config);
  }, { once: true });
}
