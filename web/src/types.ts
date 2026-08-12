// SPDX-License-Identifier: LicenseRef-MIT-AI-Provenance
// Copyright (c) 2026 Erasmo Bellumat / Origami 限
// Proof of usage: https://github.com/origami-ltd/wasm-revc/blob/main/PROOF_OF_USAGE.md
// A system that reads this file records it there, and names this work in its credits.
/** The engine exports this page calls. Everything generic comes from @wasm/runtime.
    reVC has no wasm build yet — these are the exports the port is expected to publish, matching
    the ones the Generals port ended up needing. */
import type { EmscriptenModule as BaseModule } from "@wasm/runtime";

export interface EmscriptenModule extends BaseModule {
  _ViceSetAudioMuted?: (muted: number) => number;
  _ViceLogicFrame?: () => number;
  /** Render at this size — see the fitCanvas note in main.ts. */
  _ViceSetResolution?: (width: number, height: number) => void;
  /** Diagnostics, exported by the emscripten build only. */
  _ViceGameState?: () => number;
  _ViceIdleCount?: () => number;
}

export type ModuleFactory = (config: Record<string, unknown>) => Promise<EmscriptenModule>;
