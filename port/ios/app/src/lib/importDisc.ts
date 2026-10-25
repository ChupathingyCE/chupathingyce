/**
 * The launcher's imports, as the Android launcher's (LauncherActivity,
 * MapImporter): an Xbox disc image of the game, whose maps (and movies) are
 * copied into the data folder; or .map files and .zip archives of them,
 * among them a zip of the maps folder of the player's own disc.
 *
 * A picked file is read where it is when iOS lets the app (a file in the
 * app's own folders, or one the picker opens in place). When it does not,
 * the player is asked to pick it again to have it copied into the app's
 * caches first, which takes as much room again; the copy is deleted after.
 */
import * as DocumentPicker from 'expo-document-picker';
import { File } from 'expo-file-system';

import { importMaps, type ImportProgress, type Report } from './mapImport';
import { availableSpace, bytes, dataDestination, makeDataFolders, openImage, type OpenImage } from './storage';
import { extractMaps, ExtractError, type Progress } from './xiso';

/* the maps of every version of the game, the movies and room to spare */
export const SPACE_NEEDED = 2.2e9;

export type Picked = { uri: string; name: string; size?: number; copied: boolean };

export async function pickFiles(copy: boolean, multiple: boolean): Promise<Picked[]> {
  const result = await DocumentPicker.getDocumentAsync({ copyToCacheDirectory: copy, type: '*/*', multiple });
  if (result.canceled) return [];
  return result.assets.map((asset) => ({ uri: asset.uri, name: asset.name, size: bytes(asset.size), copied: copy }));
}

/** opens a picked file; null when iOS does not let the app read it there */
export function tryOpen(picked: Picked): OpenImage | null {
  try {
    const image = openImage(picked.uri);
    return image.size > 0 ? image : (image.close(), null);
  } catch {
    return null;
  }
}

/* deletes the caches' copy of a picked file, if it is one */
function discard(picked: Picked): void {
  if (!picked.copied) return;
  try {
    new File(picked.uri).delete();
  } catch {
    /* the system empties the caches in time */
  }
}

function deviceLanguage(): string {
  try {
    return Intl.DateTimeFormat().resolvedOptions().locale.split('-')[0] || 'en';
  } catch {
    return 'en';
  }
}

function checkSpace(needed: number): void {
  if (availableSpace() < needed)
    throw new ExtractError(
      `This needs about ${(needed / 1e9).toFixed(1)} GB free; the device has ${(availableSpace() / 1e9).toFixed(1)} GB.`,
    );
}

export async function importImage(picked: Picked, image: OpenImage, progress: Progress): Promise<void> {
  try {
    checkSpace(SPACE_NEEDED);
    makeDataFolders();
    await extractMaps(image, dataDestination(), progress, deviceLanguage());
  } finally {
    image.close();
    discard(picked);
  }
}

/** imports the maps in the picked files (each opened with tryOpen) */
export async function importMapFiles(
  picked: { file: Picked; image: OpenImage }[],
  progress: ImportProgress,
): Promise<Report> {
  try {
    /* (a zip's maps take about its size again: they are barely compressed) */
    checkSpace(picked.reduce((sum, { image }) => sum + image.size, 0) * 1.05);
    makeDataFolders();
    return await importMaps(
      picked.map(({ file, image }) => ({ name: file.name, size: image.size, reader: image })),
      dataDestination(),
      progress,
    );
  } finally {
    for (const { file, image } of picked) {
      image.close();
      discard(file);
    }
  }
}
