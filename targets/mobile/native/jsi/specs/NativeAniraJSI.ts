import { TurboModule, TurboModuleRegistry } from "react-native";

/**
 * The app's native engine: one example of the examples repository (app.json's
 * expo.extra.anira.example) on anira, between the microphone and the output.
 */
export interface Spec extends TurboModule {
    /**
     * Open the audio device (microphone in, output out) and start the example.
     * Returns JSON: {success: boolean, error?: string}
     */
    startAudio(): string;

    /** Stop the audio device. */
    stopAudio(): void;

    /**
     * The example and the engine, JSON: {name, audioRunning, sampleRate,
     * latency (samples), parameters: [{name, min, max, value}],
     * meters: [{name, min, max}], backends: string[], backend (index), mix,
     * status (the backend's verdict or why the model does not run; empty when
     * it plays), waits (a backend waiting for tryNonetheless)}.
     */
    getState(): string;

    /** The example's parameter `index` (as getState lists them). */
    setParameter(index: number, value: number): void;

    /** The backend: an index into getState().backends (the last is Bypass). */
    setBackend(index: number): void;

    /** Dry/wet, 0..1. */
    setMix(value: number): void;

    /** Run the chosen backend although prepare found it not real-time here. */
    tryNonetheless(): void;

    /**
     * The last `maxCount` entries, oldest first, one per 32 ms of audio: the
     * input's peak and every meter's value at the end of that stretch.
     * Returns JSON: {peaks: number[], meters: number[][]}
     */
    getTimeline(maxCount: number): string;
}

export default TurboModuleRegistry.getEnforcing<Spec>("NativeAniraJSI");
