/**
 * Downloads a .zip of maps (or a .map) from an address the player gives,
 * into the app's caches, for importMapFiles to import and then delete. The
 * app knows no address of its own: the player brings the file, as with the
 * disc image.
 *
 * The download is an iOS background session, so it goes on while the app is
 * in the background (not if the app is closed).
 */
import { File, Paths, type DownloadTask } from 'expo-file-system';

import { downloadName } from './address';
import type { Picked } from './importDisc';
import { availableSpace, bytes } from './storage';

export { checkAddress } from './address';

export class DownloadError extends Error {
  constructor(message: string) {
    super(message);
    this.name = 'DownloadError';
  }
}

export interface Download {
  /** the downloaded file, as a picked one; null when cancelled */
  done: Promise<Picked | null>;
  cancel(): void;
}

/**
 * Starts the download. progress gets the bytes so far and the total (-1
 * when the server does not say). A file larger than half the free room is
 * stopped as soon as its size is known: its maps take that much again.
 */
export function startDownload(url: URL, progress: (written: number, total: number) => void): Download {
  const name = downloadName(url);
  const destination = new File(Paths.cache, `download-${Date.now()}-${name}`);
  let cancelled = false;
  let tooLarge = 0;

  const remove = () => {
    try {
      if (destination.exists) destination.delete();
    } catch {
      /* the system empties the caches in time */
    }
  };

  const task: DownloadTask = File.createDownloadTask(url.toString(), destination, {
    sessionType: 'background',
    onProgress: (event) => {
      const bytesWritten = bytes(event.bytesWritten);
      const totalBytes = bytes(event.totalBytes);
      if (!tooLarge && totalBytes > 0 && totalBytes * 2.05 > availableSpace() + bytesWritten) {
        tooLarge = totalBytes;
        task.cancel();
        return;
      }
      progress(bytesWritten, totalBytes);
    },
  });

  const done = (async (): Promise<Picked | null> => {
    try {
      const file = await task.downloadAsync();
      if (!file) throw new DownloadError('The download stopped.');
      return { uri: file.uri, name, size: bytes(file.size), copied: true };
    } catch (e) {
      remove();
      if (tooLarge)
        throw new DownloadError(
          `The file is ${(tooLarge / 1e9).toFixed(1)} GB: the download and its maps need about ` +
            `${((tooLarge * 2.05) / 1e9).toFixed(1)} GB free, and the device has ${(availableSpace() / 1e9).toFixed(1)} GB.`,
        );
      if (cancelled) return null;
      throw new DownloadError(`The download failed: ${e instanceof Error ? e.message : String(e)}`);
    } finally {
      task.release();
    }
  })();

  return {
    done,
    cancel: () => {
      cancelled = true;
      task.cancel();
    },
  };
}
