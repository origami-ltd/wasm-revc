/** The engine exports this page calls. Everything generic comes from @wasm/runtime.
    reVC has no wasm build yet — these are the exports the port is expected to publish, matching
    the ones the Generals port ended up needing. */
import type { EmscriptenModule as BaseModule } from "@wasm/runtime";

export interface EmscriptenModule extends BaseModule {
  _ViceSetAudioMuted?: (muted: number) => number;
  _ViceLogicFrame?: () => number;
  _VicePump?: () => void;
}

export type ModuleFactory = (config: Record<string, unknown>) => Promise<EmscriptenModule>;
