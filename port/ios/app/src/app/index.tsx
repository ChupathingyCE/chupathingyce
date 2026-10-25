/**
 * The launcher: imports the game's data on the first start, then starts the
 * game. In Expo Go the game itself is not there (it is native code, in the
 * development build to come), so Play only says so.
 */
import { activateKeepAwakeAsync, deactivateKeepAwake } from 'expo-keep-awake';
import { useCallback, useEffect, useRef, useState } from 'react';
import { ActivityIndicator, Alert, Pressable, ScrollView, StyleSheet, Text, View } from 'react-native';

import { importImage, pickImage, SPACE_NEEDED, tryOpen } from '../lib/importDisc';
import { dataRoot, hasGameData, makeDataFolders } from '../lib/storage';
import { ExtractError } from '../lib/xiso';
import { colors } from '../theme';

type State =
  | { kind: 'missing'; error?: string }
  | { kind: 'importing'; file: string; done: number; total: number }
  | { kind: 'ready' };

const KEEP_AWAKE_TAG = 'import';

function gigabytes(bytes: number): string {
  return `${(bytes / 1e9).toFixed(2)} GB`;
}

export default function Launcher() {
  const [state, setState] = useState<State>(() => (hasGameData() ? { kind: 'ready' } : { kind: 'missing' }));
  const lastReport = useRef(0);

  useEffect(() => {
    /* made on the first start, as the Android launcher does */
    makeDataFolders();
  }, []);

  const runImport = useCallback(async (copy: boolean) => {
    const picked = await pickImage(copy);
    if (!picked) return;
    const image = tryOpen(picked);
    if (!image) {
      if (copy) {
        setState({ kind: 'missing', error: 'The disc image could not be opened.' });
        return;
      }
      Alert.alert(
        'Copy the disc image first?',
        `iOS does not let the app read ${picked.name} where it is. Pick it again to copy it into the app ` +
          `first: that needs as much free room again as the image, and the copy is deleted after.`,
        [
          { text: 'Cancel', style: 'cancel' },
          { text: 'Pick again', onPress: () => void runImport(true) },
        ],
      );
      return;
    }

    setState({ kind: 'importing', file: '', done: 0, total: 0 });
    await activateKeepAwakeAsync(KEEP_AWAKE_TAG);
    try {
      await importImage(picked, image, (file, done, total) => {
        /* a report per megabyte is far more than the screen needs */
        const now = Date.now();
        if (now - lastReport.current < 200 && done < total) return;
        lastReport.current = now;
        setState({ kind: 'importing', file, done, total });
      });
      setState({ kind: 'ready' });
    } catch (e) {
      const message = e instanceof ExtractError ? e.message : `The import failed: ${String(e)}`;
      setState({ kind: 'missing', error: message });
    } finally {
      deactivateKeepAwake(KEEP_AWAKE_TAG);
    }
  }, []);

  return (
    <ScrollView contentContainerStyle={styles.container}>
      {state.kind === 'missing' && (
        <View style={styles.card}>
          <Text style={styles.title}>The game's data is not here yet</Text>
          <Text style={styles.body}>
            Pick an Xbox disc image of Halo: Combat Evolved (.xiso or .iso, any version) and the app copies its
            maps and movies, about {gigabytes(SPACE_NEEDED)} with room to spare. Bring your own copy: the app
            never downloads the game.
          </Text>
          <Text style={styles.body}>Keep the app open while it copies: iOS pauses it in the background.</Text>
          {state.error && <Text style={styles.error}>{state.error}</Text>}
          <Pressable style={styles.button} onPress={() => void runImport(false)}>
            <Text style={styles.buttonText}>Pick the disc image</Text>
          </Pressable>
        </View>
      )}

      {state.kind === 'importing' && (
        <View style={styles.card}>
          <Text style={styles.title}>Copying the game's data</Text>
          {state.total === 0 ? (
            <ActivityIndicator color={colors.accent} />
          ) : (
            <>
              <View style={styles.track}>
                <View style={[styles.bar, { width: `${(100 * state.done) / state.total}%` }]} />
              </View>
              <Text style={styles.body}>
                {state.file} · {gigabytes(state.done)} of {gigabytes(state.total)}
              </Text>
            </>
          )}
        </View>
      )}

      {state.kind === 'ready' && (
        <View style={styles.card}>
          <Text style={styles.title}>Ready</Text>
          <Text style={styles.body}>The game's data is in place.</Text>
          <Pressable
            style={styles.button}
            onPress={() =>
              Alert.alert('Not in Expo Go', 'The game is native code: it plays in the development build, not in Expo Go.')
            }>
            <Text style={styles.buttonText}>Play</Text>
          </Pressable>
          <Pressable style={styles.secondary} onPress={() => void runImport(false)}>
            <Text style={styles.secondaryText}>Import a disc image again</Text>
          </Pressable>
        </View>
      )}

      <Text style={styles.footer} selectable>
        Data folder: {dataRoot().uri}
      </Text>
    </ScrollView>
  );
}

const styles = StyleSheet.create({
  container: { padding: 16, gap: 16, maxWidth: 640, width: '100%', alignSelf: 'center' },
  card: { backgroundColor: colors.surface, borderRadius: 12, padding: 20, gap: 12 },
  title: { color: colors.text, fontSize: 20, fontWeight: '600' },
  body: { color: colors.muted, fontSize: 15, lineHeight: 21 },
  error: { color: colors.error, fontSize: 15 },
  button: { backgroundColor: colors.accent, borderRadius: 8, paddingVertical: 14, alignItems: 'center' },
  buttonText: { color: '#0b0f0b', fontSize: 16, fontWeight: '600' },
  secondary: { paddingVertical: 10, alignItems: 'center' },
  secondaryText: { color: colors.accent, fontSize: 15 },
  track: { height: 8, borderRadius: 4, backgroundColor: colors.background, overflow: 'hidden' },
  bar: { height: 8, backgroundColor: colors.accent },
  footer: { color: colors.muted, fontSize: 12, textAlign: 'center' },
});
