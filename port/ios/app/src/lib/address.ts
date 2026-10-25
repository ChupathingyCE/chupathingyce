/* the address the player gives for a download of maps (download.ts) */

/** the address as one to download from, or why it is not */
export function checkAddress(text: string): URL | string {
  let url: URL;
  try {
    url = new URL(text.trim());
  } catch {
    return 'That is not a web address.';
  }
  if (url.protocol !== 'https:' && url.protocol !== 'http:') return 'The address must start with https://.';
  return url;
}

/** the name the import goes by: the address's, a .map kept as one and
anything else taken as a .zip (the importer decides by the name) */
export function downloadName(url: URL): string {
  let last = url.pathname.split('/').pop() ?? '';
  try {
    last = decodeURIComponent(last);
  } catch {
    /* (a malformed escape: the name as it is) */
  }
  last = last.replace(/[^A-Za-z0-9._ -]/g, '_').replace(/^\.+/, '');
  if (/\.(map|zip)$/i.test(last)) return last;
  return `${last || 'download'}.zip`;
}
