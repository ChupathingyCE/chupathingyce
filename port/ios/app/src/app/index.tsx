/**
 * The launcher: imports the game's data on the first start (a disc image,
 * or a zip of the disc's maps folder), imports Halo PC maps, then starts the
 * game. In Expo Go the game itself is not there (it is native code, in the
 * development build to come), so Play only says so.
 */
import { activateKeepAwakeAsync, deactivateKeepAwake } from 'expo-keep-awake';
import { useCallback, useEffect, useRef, useState } from 'react';
import { ActivityIndicator, Alert, Pressable, ScrollView, StyleSheet, Text, View } from 'react-native';

import { importImage, importMapFiles, pickFiles, SPACE_NEEDED, tryOpen, type Picked } from '../lib/importDisc';
import { summary } from '../lib/mapImport';
import { dataRoot, hasGameData, makeDataFolders, type OpenImage } from '../lib/storage';
import { ExtractError } from '../lib/xiso';
import { colors } from '../theme';

type State =
  | { kind: 'idle'; error?: string }
  | { kind: 'importing'; title: string; status: string; done: number; total: number };

type ImportKind = 'disc' | 'maps';

const KEEP_AWAKE_TAG = 'import';

function gigabytes(bytes: number): string {
  return `${(bytes / 1e9).toFixed(2)} GB`;
}

/* runs an import with the screen kept awake */
async function keptAwake<T>(work: () => Promise<T>): Promise<T> {
  await activateKeepAwakeAsync(KEEP_AWAKE_TAG);
  try {
    return await work();
  } finally {
    deactivateKeepAwake(KEEP_AWAKE_TAG);
  }
}

export default function Launcher() {
  const [state, setState] = useState<State>({ kind: 'idle' });
  const [ready, setReady] = useState(hasGameData);
  const lastReport = useRef(0);

  useEffect(() => {
    /* made on the first start, as the Android launcher does */
    makeDataFolders();
  }, []);

  /* a report per megabyte is far more than the screen needs */
  const report = useCallback((title: string, status: string, done: number, total: number) => {
    const now = Date.now();
    if (now - lastReport.current < 200 && done < total) return;
    lastReport.current = now;
    setState({ kind: 'importing', title, status, done, total });
  }, []);

  async function runImport(kind: ImportKind, copy: boolean): Promise<void> {
    const picked = await pickFiles(copy, kind === 'maps');
    if (picked.length === 0) return;
    const opened: { file: Picked; image: OpenImage }[] = [];
    for (const file of picked) {
      const image = tryOpen(file);
      if (image) {
        opened.push({ file, image });
        continue;
      }
      for (const { image: open } of opened) open.close();
      if (copy) {
        setState({ kind: 'idle', error: `${file.name} could not be opened.` });
        return;
      }
      Alert.alert(
        'Copy the files first?',
        `iOS does not let the app read ${file.name} where it is. Pick again to copy into the app first: ` +
          `that needs as much free room again, and the copy is deleted after.`,
        [
          { text: 'Cancel', style: 'cancel' },
          { text: 'Pick again', onPress: () => void runImport(kind, true) },
        ],
      );
      return;
    }

    try {
      if (kind === 'disc') {
        const title = "Copying the game's data";
        setState({ kind: 'importing', title, status: '', done: 0, total: 0 });
        await keptAwake(() =>
          importImage(opened[0].file, opened[0].image, (file, done, total) => report(title, file, done, total)),
        );
        setState({ kind: 'idle' });
      } else {
        const title = 'Importing maps';
        setState({ kind: 'importing', title, status: '', done: 0, total: 0 });
        const result = await keptAwake(() =>
          importMapFiles(opened, (status, done, total) => report(title, status, done, total)),
        );
        setState({ kind: 'idle' });
        const disc = result.imported.some((path) => path.startsWith('maps/'));
        Alert.alert(
          'Import maps',
          summary(result) +
            (disc && !hasGameData() ? "\n\nThe disc's maps need ui.map too: it was not among them." : ''),
        );
      }
    } catch (e) {
      const message = e instanceof ExtractError ? e.message : `The import failed: ${String(e)}`;
      setState({ kind: 'idle', error: message });
    } finally {
      setReady(hasGameData());
    }
  }

  const importMapsButton = (
    <Pressable style={styles.secondary} onPress={() => void runImport('maps', false)}>
      <Text style={styles.secondaryText}>Import maps (.map or .zip)</Text>
    </Pressable>
  );

  return (
    <ScrollView contentContainerStyle={styles.container}>
      {state.kind === 'importing' && (
        <View style={styles.card}>
          <Text style={styles.title}>{state.title}</Text>
          {state.total === 0 ? (
            <ActivityIndicator color={colors.accent} />
          ) : (
            <>
              <View style={styles.track}>
                <View style={[styles.bar, { width: `${(100 * state.done) / state.total}%` }]} />
              </View>
              <Text style={styles.body}>
                {state.status} · {gigabytes(state.done)} of {gigabytes(state.total)}
              </Text>
            </>
          )}
          <Text style={styles.body}>Keep the app open: iOS pauses it in the background.</Text>
        </View>
      )}

      {state.kind === 'idle' && !ready && (
        <View style={styles.card}>
          <Text style={styles.title}>{"The game's data is not here yet"}</Text>
          <Text style={styles.body}>
            Pick an Xbox disc image of Halo: Combat Evolved (.xiso or .iso, any version) and the app copies its
            maps and movies, about {gigabytes(SPACE_NEEDED)} with room to spare. Or pick a .zip of the maps folder
            from your own disc. Bring your own copy: the app never downloads the game.
          </Text>
          {state.error && <Text style={styles.error}>{state.error}</Text>}
          <Pressable style={styles.button} onPress={() => void runImport('disc', false)}>
            <Text style={styles.buttonText}>Pick the disc image</Text>
          </Pressable>
          <Pressable style={styles.secondary} onPress={() => void runImport('maps', false)}>
            <Text style={styles.secondaryText}>Pick a zip of the maps folder</Text>
          </Pressable>
        </View>
      )}

      {state.kind === 'idle' && ready && (
        <View style={styles.card}>
          <Text style={styles.title}>Ready</Text>
          <Text style={styles.body}>{"The game's data is in place."}</Text>
          {state.error && <Text style={styles.error}>{state.error}</Text>}
          <Pressable
            style={styles.button}
            onPress={() =>
              Alert.alert('Not in Expo Go', 'The game is native code: it plays in the development build, not in Expo Go.')
            }>
            <Text style={styles.buttonText}>Play</Text>
          </Pressable>
          {importMapsButton}
          <Pressable style={styles.secondary} onPress={() => void runImport('disc', false)}>
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
