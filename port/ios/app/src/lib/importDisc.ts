/**
 * The first start's import, as the Android launcher's (LauncherActivity):
 * the player picks an Xbox disc image of the game with the system picker,
 * and its maps (and movies) are copied into the data folder.
 *
 * The image is read where it is when iOS lets the app (a file in the app's
 * own folders, or one the picker opens in place). When it does not, the
 * player is asked to pick it again to have it copied into the app's caches
 * first, which takes as much room again as the image; the copy is deleted
 * after.
 */
import * as DocumentPicker from 'expo-document-picker';
import { File } from 'expo-file-system';

import { availableSpace, dataDestination, makeDataFolders, openImage, type OpenImage } from './storage';
import { extractMaps, ExtractError, type Progress } from './xiso';

/* the maps of every version of the game, the movies and room to spare */
export const SPACE_NEEDED = 2.2e9;

export type Picked = { uri: string; name: string; size?: number; copied: boolean };

export async function pickImage(copy: boolean): Promise<Picked | null> {
  const result = await DocumentPicker.getDocumentAsync({ copyToCacheDirectory: copy, type: '*/*' });
  if (result.canceled) return null;
  const asset = result.assets[0];
  return { uri: asset.uri, name: asset.name, size: asset.size, copied: copy };
}

/** opens the picked image; null when iOS does not let the app read it there */
export function tryOpen(picked: Picked): OpenImage | null {
  try {
    const image = openImage(picked.uri);
    return image.size > 0 ? image : (image.close(), null);
  } catch {
    return null;
  }
}

function deviceLanguage(): string {
  try {
    return Intl.DateTimeFormat().resolvedOptions().locale.split('-')[0] || 'en';
  } catch {
    return 'en';
  }
}

export async function importImage(picked: Picked, image: OpenImage, progress: Progress): Promise<void> {
  try {
    if (availableSpace() < SPACE_NEEDED)
      throw new ExtractError(
        `The game's data needs about ${(SPACE_NEEDED / 1e9).toFixed(1)} GB free; ` +
          `this device has ${(availableSpace() / 1e9).toFixed(1)} GB.`,
      );
    makeDataFolders();
    await extractMaps(image, dataDestination(), progress, deviceLanguage());
  } finally {
    image.close();
    if (picked.copied) {
      try {
        new File(picked.uri).delete();
      } catch {
        /* the system empties the caches in time */
      }
    }
  }
}
