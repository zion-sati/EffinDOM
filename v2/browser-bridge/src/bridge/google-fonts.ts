export interface GoogleFontShardBytes {
  readonly url: string;
  readonly bytes: Uint8Array;
}

const GOOGLE_FONTS_STYLESHEET_ORIGIN = 'https://fonts.googleapis.com';
const GOOGLE_FONTS_BINARY_ORIGIN = 'https://fonts.gstatic.com';

function ensureLinkTag(rel: string, href: string, crossOrigin?: '' | 'anonymous'): void {
  const existing = document.head.querySelector(`link[rel="${rel}"][href="${href}"]`);
  if (existing instanceof HTMLLinkElement) {
    return;
  }
  const link = document.createElement('link');
  link.rel = rel;
  link.href = href;
  if (crossOrigin !== undefined) {
    link.crossOrigin = crossOrigin;
  }
  document.head.appendChild(link);
}

export function ensureGoogleFontShardPreconnect(): void {
  ensureLinkTag('preconnect', GOOGLE_FONTS_STYLESHEET_ORIGIN);
  ensureLinkTag('preconnect', GOOGLE_FONTS_BINARY_ORIGIN, 'anonymous');
}

export function buildGoogleFontStylesheetUrl(googleFamily: string, text: string): string {
  const params = new URLSearchParams();
  params.set('family', `${googleFamily}:wght@400`);
  params.set('text', text);
  params.set('display', 'swap');
  return `${GOOGLE_FONTS_STYLESHEET_ORIGIN}/css2?${params.toString()}`;
}

export function parseGoogleFontBinaryUrl(stylesheetText: string): string {
  const match = /src:\s*url\(([^)]+)\)\s*format\((['"]?)(woff2|woff|truetype|opentype)\2\)/.exec(stylesheetText);
  if (match?.[1] === undefined) {
    throw new Error('Google Fonts stylesheet did not expose a usable shard URL.');
  }
  return match[1].trim();
}

export async function fetchGoogleFontShardBytes(
  googleFamily: string,
  text: string,
): Promise<GoogleFontShardBytes> {
  ensureGoogleFontShardPreconnect();
  const stylesheetUrl = buildGoogleFontStylesheetUrl(googleFamily, text);
  const stylesheetResponse = await fetch(stylesheetUrl, { mode: 'cors' });
  if (!stylesheetResponse.ok) {
    throw new Error(`Failed to fetch Google Fonts stylesheet ${stylesheetUrl}: ${String(stylesheetResponse.status)}`);
  }
  const binaryUrl = parseGoogleFontBinaryUrl(await stylesheetResponse.text());
  const binaryResponse = await fetch(binaryUrl);
  if (!binaryResponse.ok) {
    throw new Error(`Failed to fetch font ${binaryUrl}: ${String(binaryResponse.status)}`);
  }
  return {
    url: binaryUrl,
    bytes: new Uint8Array(await binaryResponse.arrayBuffer()),
  };
}
