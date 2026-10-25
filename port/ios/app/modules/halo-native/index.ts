/**
 * The game's native side (ios/HaloNativeModule.swift). It is in the
 * development build, not in Expo Go: halo is null there.
 */
import { requireOptionalNativeModule } from 'expo';

export interface Reservation {
  ok: boolean;
  base?: string;
  aligned?: boolean;
  step?: string;
  error?: string;
}

export interface MemoryProbe {
  /** 4 GB reserved and aligned, with 512 MB committed in it: the 64-bit builds' layout */
  addressSpace: Reservation;
  /** 4 GB reserved as it comes, with 512 MB committed in it */
  direct: Reservation;
  /** 4 GB asked for at 4 GB-aligned addresses, with 512 MB committed in it */
  alignedHint: Reservation & { tried?: number };
  /** a 256 MB reservation: the console window's */
  window: { ok: boolean; base?: string; belowFourGigabytes?: boolean; error?: string };
  /** the largest single reservation, to the nearest 256 MB */
  largestReservation: number;
  /** what the app may use before iOS stops it */
  availableMemory: number;
  physicalMemory: number;
  system: string;
  model: string;
}

interface HaloNative {
  probeMemory(): Promise<MemoryProbe>;
  /** starts the game; it takes over the app and does not return, so the
  promise only rejects, with the reason the game could not start */
  start(): Promise<never>;
}

export const halo = requireOptionalNativeModule<HaloNative>('HaloNative');
