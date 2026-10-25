/**
 * The launcher: imports the game's data on the first start (a disc image,
 * or a zip of the disc's maps folder), imports Halo PC maps, then starts the
 * game. In Expo Go the game itself is not there (it is native code, in the
 * development build to come), so Play only says so.
 */
import { activateKeepAwakeAsync, deactivateKeepAwake } from 'expo-keep-awake';
import { useCallback, useEffect, useRef, useState } from 'react';
import {
  ActivityIndicator,
  Alert,
  Image,
  Pressable,
  ScrollView,
  StyleSheet,
  Text,
  TextInput,
  View,
  type ScrollViewInstance,
} from 'react-native';

import { halo } from '../../modules/halo-native';
import { MemoryCheck } from '../components/MemoryCheck';
import { checkAddress, DownloadError, startDownload, type Download } from '../lib/download';
import { importImage, importMapFiles, pickFiles, SPACE_NEEDED, tryOpen, type Picked } from '../lib/importDisc';
import { summary } from '../lib/mapImport';
import { dataRoot, hasGameData, makeDataFolders, type OpenImage } from '../lib/storage';
import { ExtractError } from '../lib/xiso';
import { colors } from '../theme';

type State =
  | { kind: 'idle'; error?: string }
  | { kind: 'importing'; title: string; status: string; done: number; total: number; cancel?: () => void };

type ImportKind = 'disc' | 'maps';

const KEEP_AWAKE_TAG = 'import';

/* the launcher's background (assets/art/background.svg, rendered) */
const BACKGROUND = require('../../assets/background.png');

/* a line at the top, a new one each time the app opens */
const QUIPS = [
  'Leave the plasma pistol. Take the gabagool.',
  'Fuhgeddaboudit. The maps are in good hands.',
  'Nobody touches ui.map. Capisce?',
  'Respawn? In this family, we drive.',
  'I got a buggy, a jacket and a bad feeling about this ring.',
  'Back in the day, a man copied his own disc.',
  'You come into my garage, on the day of the LAN party...',
  'With all due respect, the Flood are not family.',
];

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
  const [address, setAddress] = useState('');
  const [quip] = useState(() => QUIPS[Math.floor(Math.random() * QUIPS.length)]);
  const lastReport = useRef(0);
  const scroll = useRef<ScrollViewInstance>(null);

  useEffect(() => {
    /* made on the first start, as the Android launcher does */
    makeDataFolders();
  }, []);

  /* a report per megabyte is far more than the screen needs */
  const report = useCallback((title: string, status: string, done: number, total: number, cancel?: () => void) => {
    const now = Date.now();
    if (now - lastReport.current < 200 && done < total) return;
    lastReport.current = now;
    setState({ kind: 'importing', title, status, done, total, cancel });
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
        await importOpened(opened);
      }
    } catch (e) {
      const message = e instanceof ExtractError ? e.message : `The import failed: ${String(e)}`;
      setState({ kind: 'idle', error: message });
    } finally {
      setReady(hasGameData());
    }
  }

  /* starts the game (modules/halo-native), which takes over the app: the
  promise settles only if it cannot start */
  function play(): void {
    if (!halo) {
      Alert.alert('Not in Expo Go', 'The game is native code: it plays in the development build, not in Expo Go.');
      return;
    }
    halo.start().catch((e: unknown) => {
      Alert.alert('The game could not start', e instanceof Error ? e.message : String(e));
    });
  }

  /* imports the maps in opened files, then says what it did */
  async function importOpened(opened: { file: Picked; image: OpenImage }[]): Promise<void> {
    const title = 'Importing maps';
    setState({ kind: 'importing', title, status: '', done: 0, total: 0 });
    const result = await keptAwake(() =>
      importMapFiles(opened, (status, done, total) => report(title, status, done, total)),
    );
    setState({ kind: 'idle' });
    const disc = result.imported.some((path) => path.startsWith('maps/'));
    Alert.alert(
      'Import maps',
      summary(result) + (disc && !hasGameData() ? "\n\nThe disc's maps need ui.map too: it was not among them." : ''),
    );
  }

  /* downloads a .zip of maps (or a .map) from the address the player gave,
  then imports it as a picked one */
  async function runDownload(): Promise<void> {
    const url = checkAddress(address);
    if (typeof url === 'string') {
      setState({ kind: 'idle', error: url });
      return;
    }
    const title = 'Downloading';
    let download: Download | null = null;
    const cancel = () => download?.cancel();
    setState({ kind: 'importing', title, status: url.host, done: 0, total: 0, cancel });
    download = startDownload(url, (written, total) => report(title, url.host, written, Math.max(total, 0), cancel));
    try {
      const file = await keptAwake(() => download.done);
      if (!file) {
        setState({ kind: 'idle' });
        return;
      }
      const image = tryOpen(file);
      if (!image) throw new DownloadError('The downloaded file could not be opened.');
      await importOpened([{ file, image }]);
    } catch (e) {
      const message =
        e instanceof DownloadError || e instanceof ExtractError ? e.message : `The import failed: ${String(e)}`;
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
    <View style={styles.background}>
      <Image source={BACKGROUND} style={StyleSheet.absoluteFill} resizeMode="cover" />
      <ScrollView
        ref={scroll}
        contentContainerStyle={styles.container}
        /* (the keyboard's height is added at the end, so the address field
        can scroll above it) */
        automaticallyAdjustKeyboardInsets
        keyboardShouldPersistTaps="handled"
        keyboardDismissMode="interactive">
        <Text style={styles.quip}>{quip}</Text>

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
            <Text style={styles.body}>
              {state.cancel
                ? 'The download goes on in the background, but not if the app is closed.'
                : 'Keep the app open: iOS pauses it in the background.'}
            </Text>
            {state.cancel && (
              <Pressable style={styles.secondary} onPress={state.cancel}>
                <Text style={styles.secondaryText}>Cancel</Text>
              </Pressable>
            )}
          </View>
        )}

        {state.kind === 'idle' && !ready && (
          <View style={styles.card}>
            <Text style={styles.title}>{"The game's data is not here yet"}</Text>
            <Text style={styles.body}>
              Pick an Xbox disc image of Halo: Combat Evolved (.xiso or .iso, any version) and the app copies its
              maps and movies, about {gigabytes(SPACE_NEEDED)} with room to spare. Or pick a .zip of the maps folder
              from your own disc, or give the address of one below. Bring your own copy: the app comes with none of
              the game&apos;s files.
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
            <Pressable style={styles.button} onPress={play}>
              <Text style={styles.buttonText}>Play</Text>
            </Pressable>
            {importMapsButton}
            <Pressable style={styles.secondary} onPress={() => void runImport('disc', false)}>
              <Text style={styles.secondaryText}>Import a disc image again</Text>
            </Pressable>
          </View>
        )}

        {state.kind === 'idle' && (
          <View style={styles.card}>
            <Text style={styles.title}>Download maps</Text>
            <Text style={styles.body}>
              The address of a .zip of maps (a maps folder, or Halo PC maps), or of a .map. It is downloaded, its
              maps imported as a picked file&apos;s are, and the download deleted.
            </Text>
            <TextInput
              style={styles.input}
              value={address}
              onChangeText={setAddress}
              placeholder="https://example.com/maps.zip"
              placeholderTextColor={colors.muted}
              autoCapitalize="none"
              autoCorrect={false}
              keyboardType="url"
              textContentType="URL"
              returnKeyType="go"
              onFocus={() => setTimeout(() => scroll.current?.scrollToEnd({ animated: true }), 300)}
              onSubmitEditing={() => void runDownload()}
            />
            <Pressable
              style={[styles.button, address.trim() === '' && styles.disabled]}
              disabled={address.trim() === ''}
              onPress={() => void runDownload()}>
              <Text style={styles.buttonText}>Download and import</Text>
            </Pressable>
          </View>
        )}

        {state.kind === 'idle' && <MemoryCheck />}

        <Text style={styles.footer} selectable>
          Data folder: {dataRoot().uri}
        </Text>
      </ScrollView>
    </View>
  );
}

const styles = StyleSheet.create({
  background: { flex: 1, backgroundColor: colors.background },
  container: { padding: 16, gap: 16, maxWidth: 640, width: '100%', alignSelf: 'center' },
  card: { backgroundColor: colors.card, borderRadius: 12, padding: 20, gap: 12 },
  quip: { color: colors.neon, fontSize: 17, fontStyle: 'italic', textAlign: 'center', paddingVertical: 8 },
  title: { color: colors.text, fontSize: 20, fontWeight: '600' },
  body: { color: colors.muted, fontSize: 15, lineHeight: 21 },
  error: { color: colors.error, fontSize: 15 },
  button: { backgroundColor: colors.accent, borderRadius: 8, paddingVertical: 14, alignItems: 'center' },
  buttonText: { color: '#0b0f0b', fontSize: 16, fontWeight: '600' },
  secondary: { paddingVertical: 10, alignItems: 'center' },
  secondaryText: { color: colors.accent, fontSize: 15 },
  track: { height: 8, borderRadius: 4, backgroundColor: colors.background, overflow: 'hidden' },
  bar: { height: 8, backgroundColor: colors.accent },
  disabled: { opacity: 0.4 },
  input: {
    color: colors.text,
    backgroundColor: colors.background,
    borderRadius: 8,
    paddingHorizontal: 12,
    paddingVertical: 12,
    fontSize: 15,
  },
  footer: { color: colors.muted, fontSize: 12, textAlign: 'center' },
});
