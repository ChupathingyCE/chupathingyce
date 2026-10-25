/**
 * The development build's check of the memory iOS gives the app, which
 * decides how the game is built for iOS (modules/halo-native). Not shown in
 * Expo Go, which has no such module.
 */
import { useState } from 'react';
import { Pressable, Share, StyleSheet, Text, View } from 'react-native';

import { halo, type MemoryProbe, type Reservation } from '../../modules/halo-native';
import { colors } from '../theme';

function gigabytes(bytes: number): string {
  return `${(bytes / 2 ** 30).toFixed(2)} GB`;
}

function reservation(result: Reservation | undefined): string {
  /* (an older build of the native side has no such result) */
  if (!result) return 'not in this build';
  if (!result.ok) return `no (${result.step}: ${result.error})`;
  return `yes, at ${result.base}${result.aligned ? ', aligned' : ''}`;
}

export function report(probe: MemoryProbe): string {
  const window = probe.window;
  return [
    `${probe.model}, ${probe.system}`,
    `4 GB via 8 GB: ${reservation(probe.addressSpace)}`,
    `4 GB as it comes: ${reservation(probe.direct)}`,
    `4 GB aligned (hint): ${reservation(probe.alignedHint)}`,
    `256 MB window: ${window.ok ? `at ${window.base}${window.belowFourGigabytes ? ' (below 4 GB)' : ''}` : `no (${window.error})`}`,
    `largest reservation: ${gigabytes(probe.largestReservation)}`,
    `memory the app may use: ${gigabytes(probe.availableMemory)} of ${gigabytes(probe.physicalMemory)}`,
  ].join('\n');
}

export function MemoryCheck() {
  const [text, setText] = useState<string | null>(null);
  if (!halo) return null;
  const native = halo;

  async function run() {
    setText('Checking...');
    try {
      setText(report(await native.probeMemory()));
    } catch (e) {
      setText(`The check failed: ${String(e)}`);
    }
  }

  return (
    <View style={styles.card}>
      <Text style={styles.title}>Memory check</Text>
      <Text style={styles.body}>What iOS lets the game reserve: this decides how the game is built for iOS.</Text>
      {text && (
        <Text style={styles.result} selectable>
          {text}
        </Text>
      )}
      <Pressable style={styles.button} onPress={() => void run()}>
        <Text style={styles.buttonText}>Run the check</Text>
      </Pressable>
      {text && text !== 'Checking...' && (
        <Pressable style={styles.secondary} onPress={() => void Share.share({ message: text })}>
          <Text style={styles.secondaryText}>Share the result</Text>
        </Pressable>
      )}
    </View>
  );
}

const styles = StyleSheet.create({
  card: { backgroundColor: colors.surface, borderRadius: 12, padding: 20, gap: 12 },
  title: { color: colors.text, fontSize: 20, fontWeight: '600' },
  body: { color: colors.muted, fontSize: 15, lineHeight: 21 },
  result: { color: colors.text, fontSize: 13, fontFamily: 'Menlo', lineHeight: 19 },
  button: { backgroundColor: colors.accent, borderRadius: 8, paddingVertical: 14, alignItems: 'center' },
  buttonText: { color: '#0b0f0b', fontSize: 16, fontWeight: '600' },
  secondary: { paddingVertical: 10, alignItems: 'center' },
  secondaryText: { color: colors.accent, fontSize: 15 },
});
