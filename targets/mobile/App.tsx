import { StatusBar } from "expo-status-bar";
import { useCallback, useEffect, useRef, useState } from "react";
import {
    PanResponder,
    PermissionsAndroid,
    Platform,
    Pressable,
    SafeAreaView,
    ScrollView,
    StyleSheet,
    Text,
    View,
} from "react-native";

import NativeAniraJSI from "./native/jsi/specs/NativeAniraJSI";

interface Range {
    name: string;
    min: number;
    max: number;
}

interface EngineState {
    name: string;
    analyzer: boolean;
    audioRunning: boolean;
    sampleRate: number;
    latency: number;
    parameters: (Range & { value: number; choices: string[] })[];
    meters: Range[];
    backends: string[];
    backend: number;
    mix: number;
    status: string;
    waits: boolean;
    transcript: { text: string; seconds: number; speaker: number; final: boolean }[];
}

/** Bars in the scrolling graphs — 120 entries x 32 ms ≈ the last 4 seconds. */
const BUCKETS = 120;
/** Peak plot range: -60 dBFS (bottom) to 0 dBFS (top). */
const DB_FLOOR = -60;

/** Linear peak [0,1] -> dBFS. */
const toDb = (p: number) => 20 * Math.log10(Math.max(p, 1e-6));

/** Linear peak [0,1] -> normalized position [0,1] over the plot's dB range. */
const peakToDb = (p: number) => Math.max(0, Math.min(1, (toDb(p) - DB_FLOOR) / -DB_FLOOR));

const requestMicPermission = async (): Promise<boolean> =>
    Platform.OS !== "android" || // iOS prompts by itself when capture starts
    (await PermissionsAndroid.request(PermissionsAndroid.PERMISSIONS.RECORD_AUDIO)) ===
        PermissionsAndroid.RESULTS.GRANTED;

/**
 * Scrolling bar graph of normalized values [0,1], right-aligned to the newest
 * entry. Pure core-RN — the bars are Views.
 */
const Graph = ({
    values,
    axisTop,
    axisBottom,
}: {
    values: number[];
    axisTop: string;
    axisBottom: string;
}) => (
    <View style={styles.graph}>
        <View style={styles.bars}>
            {Array.from({ length: Math.max(0, BUCKETS - values.length) }, (_, i) => (
                <View key={`l${i}`} style={styles.bar} />
            ))}
            {values.map((v, i) => (
                <View
                    key={i}
                    style={[
                        styles.bar,
                        styles.barValue,
                        { height: `${Math.max(2, Math.min(1, Math.max(0, v)) * 100)}%` },
                    ]}
                />
            ))}
        </View>
        <Text style={[styles.axisLabel, { top: 4 }]}>{axisTop}</Text>
        <Text style={[styles.axisLabel, { bottom: 4 }]}>{axisBottom}</Text>
    </View>
);

/** A horizontal slider from core-RN views: drag or tap anywhere on the track. */
const Slider = ({
    min,
    max,
    value,
    onChange,
}: {
    min: number;
    max: number;
    value: number;
    onChange: (value: number) => void;
}) => {
    const width = useRef(1);
    const change = useRef(onChange);
    change.current = onChange;
    const toValue = (x: number) => min + (max - min) * Math.max(0, Math.min(1, x / width.current));
    const responder = useRef(
        PanResponder.create({
            onStartShouldSetPanResponder: () => true,
            onMoveShouldSetPanResponder: () => true,
            onPanResponderGrant: (e) => change.current(toValue(e.nativeEvent.locationX)),
            onPanResponderMove: (e) => change.current(toValue(e.nativeEvent.locationX)),
        })
    ).current;
    const fraction = max > min ? (value - min) / (max - min) : 0;
    return (
        <View
            style={styles.slider}
            onLayout={(e) => (width.current = Math.max(1, e.nativeEvent.layout.width))}
            {...responder.panHandlers}
        >
            <View style={styles.sliderTrack} pointerEvents="none">
                <View style={[styles.sliderFill, { width: `${fraction * 100}%` }]} />
            </View>
        </View>
    );
};

const Row = ({ label, value }: { label: string; value: string }) => (
    <View style={styles.row}>
        <Text style={styles.dim}>{label}</Text>
        <Text style={styles.value}>{value}</Text>
    </View>
);

export default function App() {
    const [state, setState] = useState<EngineState | null>(null);
    const [peaks, setPeaks] = useState<number[]>([]);
    const [meters, setMeters] = useState<number[][]>([]);
    const [values, setValues] = useState<number[]>([]);
    const [mix, setMix] = useState(1);
    const [error, setError] = useState<string | null>(null);

    // Poll the native engine while mounted.
    useEffect(() => {
        let first = true;
        const interval = setInterval(() => {
            try {
                const next = JSON.parse(NativeAniraJSI.getState()) as EngineState;
                setState(next);
                if (first) {
                    setValues(next.parameters.map((p) => p.value));
                    setMix(next.mix);
                    first = false;
                }
                if (next.audioRunning) {
                    const t = JSON.parse(NativeAniraJSI.getTimeline(BUCKETS));
                    setPeaks(t.peaks ?? []);
                    setMeters(t.meters ?? []);
                }
            } catch {} // native module not ready yet
        }, 100);
        return () => clearInterval(interval);
    }, []);

    const call = useCallback((json: string) => {
        const result = JSON.parse(json);
        setError(result.success ? null : (result.error ?? "unknown error"));
    }, []);

    const startAudio = useCallback(async () => {
        if (!(await requestMicPermission())) return setError("microphone permission denied");
        call(NativeAniraJSI.startAudio());
    }, [call]);

    const stopAudio = useCallback(() => {
        NativeAniraJSI.stopAudio();
    }, []);

    const setParameter = useCallback((index: number, value: number) => {
        NativeAniraJSI.setParameter(index, value);
        setValues((old) => old.map((v, i) => (i === index ? value : v)));
    }, []);

    const running = state?.audioRunning ?? false;
    const lastPeak = peaks[peaks.length - 1];
    const peakDb = lastPeak !== undefined ? `${toDb(lastPeak).toFixed(1)} dB` : "-";
    const latencyMs =
        running && state!.sampleRate > 0 ? `${((1000 * state!.latency) / state!.sampleRate).toFixed(1)} ms` : "-";

    return (
        <SafeAreaView style={styles.container}>
            <StatusBar style="light" />
            <ScrollView contentContainerStyle={styles.content}>
                <View style={styles.header}>
                    <Text style={styles.title}>anira</Text>
                    <Text style={styles.dim}>react native example</Text>
                </View>
                <Text style={styles.dim}>{state?.name ?? ""}</Text>

                <View style={styles.card}>
                    <View style={styles.row}>
                        <Text style={styles.dim}>backend</Text>
                        <View style={styles.segment}>
                            {(state?.backends ?? []).map((name, index) => (
                                <Pressable
                                    key={name}
                                    style={[
                                        styles.segmentItem,
                                        state!.backend === index && styles.segmentActive,
                                    ]}
                                    onPress={() => NativeAniraJSI.setBackend(index)}
                                >
                                    <Text
                                        style={[
                                            styles.segmentText,
                                            state!.backend === index && { color: "#fff" },
                                        ]}
                                    >
                                        {name}
                                    </Text>
                                </Pressable>
                            ))}
                        </View>
                    </View>
                    <Row label="sample rate" value={running ? `${state!.sampleRate} Hz` : "-"} />
                    <Row label="latency" value={latencyMs} />
                    {!!state?.status && <Text style={styles.hint}>{state.status}</Text>}
                    {state?.waits && (
                        <Pressable style={styles.smallButton} onPress={() => NativeAniraJSI.tryNonetheless()}>
                            <Text style={styles.buttonText}>try nonetheless</Text>
                        </Pressable>
                    )}
                </View>

                <View style={styles.card}>
                    {(state?.parameters ?? []).map((p, i) =>
                        p.choices.length > 0 ? (
                            <View key={p.name} style={styles.row}>
                                <Text style={styles.dim}>{p.name.toLowerCase()}</Text>
                                <View style={styles.segment}>
                                    {p.choices.map((choice, k) => (
                                        <Pressable
                                            key={choice}
                                            style={[
                                                styles.segmentItem,
                                                Math.round(values[i] ?? p.value) === k && styles.segmentActive,
                                            ]}
                                            onPress={() => setParameter(i, k)}
                                        >
                                            <Text style={styles.segmentText}>{choice}</Text>
                                        </Pressable>
                                    ))}
                                </View>
                            </View>
                        ) : (
                        <View key={p.name} style={styles.control}>
                            <Row label={p.name.toLowerCase()} value={(values[i] ?? p.value).toFixed(2)} />
                            <Slider
                                min={p.min}
                                max={p.max}
                                value={values[i] ?? p.value}
                                onChange={(v) => setParameter(i, v)}
                            />
                        </View>
                        )
                    )}
                    {!state?.analyzer && <View style={styles.control}>
                        <Row label="dry/wet" value={`${Math.round(mix * 100)} %`} />
                        <Slider
                            min={0}
                            max={1}
                            value={mix}
                            onChange={(v) => {
                                setMix(v);
                                NativeAniraJSI.setMix(v);
                            }}
                        />
                    </View>}
                </View>

                <View style={styles.card}>
                    <Row label="input peak" value={peakDb} />
                    <Graph values={peaks.map(peakToDb)} axisTop="0 dB" axisBottom="-60 dB" />
                    {(state?.meters ?? []).map((m, i) => {
                        const timeline = meters[i] ?? [];
                        const last = timeline[timeline.length - 1];
                        const span = m.max - m.min || 1;
                        return (
                            <View key={m.name} style={styles.control}>
                                <Row
                                    label={m.name.toLowerCase()}
                                    value={last !== undefined ? last.toFixed(2) : "-"}
                                />
                                <Graph
                                    values={timeline.map((v) => (v - m.min) / span)}
                                    axisTop={`${m.max}`}
                                    axisBottom={`${m.min}`}
                                />
                            </View>
                        );
                    })}
                    <Text style={styles.hint}>
                        one bar per 32 ms; the example runs between the microphone and the
                        output (use headphones)
                    </Text>
                </View>

                {(state?.transcript.length ?? 0) > 0 && (
                    <View style={styles.card}>
                        {state!.transcript.slice(-12).map((line, i) => (
                            <Text key={i} style={[styles.value, !line.final && { opacity: 0.6 }]}>
                                <Text style={styles.dim}>{line.seconds.toFixed(1)} s  </Text>
                                {line.speaker >= 0 ? `speaker ${line.speaker}: ` : ""}
                                {line.text}
                            </Text>
                        ))}
                    </View>
                )}

                {error && <Text style={styles.error}>{error}</Text>}

                <Pressable style={styles.button} onPress={running ? stopAudio : startAudio}>
                    <Text style={styles.buttonText}>{running ? "stop audio" : "start audio"}</Text>
                </Pressable>
            </ScrollView>
        </SafeAreaView>
    );
}

const styles = StyleSheet.create({
    container: { flex: 1, backgroundColor: "#101014" },
    content: { padding: 24, gap: 16 },
    control: { gap: 8 },
    slider: { height: 28, justifyContent: "center" },
    sliderTrack: { height: 6, borderRadius: 3, backgroundColor: "#26262e", overflow: "hidden" },
    sliderFill: { height: "100%", backgroundColor: "#4c6ef5" },
    smallButton: { backgroundColor: "#26262e", borderRadius: 8, paddingVertical: 8, alignItems: "center" },
    title: { color: "#f2f2f7", fontSize: 40, fontWeight: "700", letterSpacing: -1 },
    card: { backgroundColor: "#1a1a20", borderRadius: 14, padding: 16, gap: 10 },
    row: { flexDirection: "row", justifyContent: "space-between", alignItems: "center" },
    header: { flexDirection: "row", alignItems: "baseline", gap: 10 },
    dim: { color: "#8e8e99", fontSize: 14 },
    value: { color: "#f2f2f7", fontSize: 14, fontVariant: ["tabular-nums"] },
    hint: { color: "#5a5a66", fontSize: 12, lineHeight: 16 },
    graph: {
        height: 90,
        borderRadius: 10,
        backgroundColor: "#16161c",
        overflow: "hidden",
        justifyContent: "flex-end",
    },
    bars: { flexDirection: "row", alignItems: "flex-end", height: "100%", paddingHorizontal: 2 },
    bar: { flex: 1, marginHorizontal: 0.5, borderRadius: 1 },
    barValue: { backgroundColor: "#4c6ef5", opacity: 0.75 },
    axisLabel: {
        position: "absolute",
        left: 6,
        color: "#5a5a66",
        fontSize: 10,
        fontVariant: ["tabular-nums"],
    },
    segment: { flexDirection: "row", backgroundColor: "#26262e", borderRadius: 8, padding: 2 },
    segmentItem: { paddingVertical: 4, paddingHorizontal: 10, borderRadius: 6 },
    segmentActive: { backgroundColor: "#4c6ef5" },
    segmentText: { color: "#8e8e99", fontSize: 12, fontWeight: "600" },
    error: { color: "#ff6b6b", fontSize: 13 },
    button: { backgroundColor: "#4c6ef5", borderRadius: 12, paddingVertical: 14, alignItems: "center" },
    buttonText: { color: "#ffffff", fontSize: 16, fontWeight: "600" },
});
