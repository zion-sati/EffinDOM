import type { ClipboardRichTextPart, ClipboardRichTextPayload, ClipboardWritePayload } from './core-types';

export const EFFINDOM_RICH_TEXT_CLIPBOARD_MIME = 'web application/x-effindom-richtext+json';

function escapeHtml(text: string): string {
  return text
    .replace(/&/g, '&amp;')
    .replace(/</g, '&lt;')
    .replace(/>/g, '&gt;')
    .replace(/"/g, '&quot;')
    .replace(/'/g, '&#39;');
}

function colorToCss(color: number): string {
  const red = (color >>> 24) & 0xff;
  const green = (color >>> 16) & 0xff;
  const blue = (color >>> 8) & 0xff;
  const alpha = color & 0xff;
  if (alpha >= 0xff) {
    return `rgb(${String(red)} ${String(green)} ${String(blue)})`;
  }
  return `rgb(${String(red)} ${String(green)} ${String(blue)} / ${(alpha / 255).toFixed(3)})`;
}

function inferClipboardCssFontFamily(fontUrl: string | undefined): string | null {
  if (fontUrl === undefined) {
    return null;
  }
  if (/mono/i.test(fontUrl)) {
    return 'monospace';
  }
  return null;
}

function partStyleToCss(part: ClipboardRichTextPart): string {
  const styles: string[] = [];
  if (part.color !== undefined) {
    styles.push(`color: ${colorToCss(part.color)};`);
  }
  if (part.bgColor !== undefined && part.bgColor !== 0) {
    styles.push(`background-color: ${colorToCss(part.bgColor)};`);
  }
  if (part.fontSize !== undefined) {
    styles.push(`font-size: ${String(part.fontSize)}px;`);
  }
  const fontFamily = inferClipboardCssFontFamily(part.fontUrl);
  if (fontFamily !== null) {
    styles.push(`font-family: ${fontFamily};`);
  }
  const decorationFlags = part.decorationFlags ?? 0;
  const textDecorations: string[] = [];
  if ((decorationFlags & 1) !== 0) {
    textDecorations.push('underline');
  }
  if ((decorationFlags & 2) !== 0) {
    textDecorations.push('line-through');
  }
  if (textDecorations.length > 0) {
    styles.push(`text-decoration: ${textDecorations.join(' ')};`);
  }
  return styles.join(' ');
}

function buildClipboardHtml(richText: ClipboardRichTextPayload): string {
  let html = '<div data-effindom-richtext="1" style="white-space: pre-wrap;">';
  for (const part of richText.parts) {
    const escapedText = escapeHtml(part.text);
    const style = partStyleToCss(part);
    if (style.length > 0) {
      html += `<span style="${style}">${escapedText}</span>`;
      continue;
    }
    html += escapedText;
  }
  html += '</div>';
  return html;
}

export function enrichClipboardPayload(
  payload: ClipboardWritePayload,
  resolveFontUrl: (fontId: number) => string | null,
): ClipboardWritePayload {
  const richText = payload.richText;
  if (richText === undefined) {
    return payload;
  }
  return {
    plainText: payload.plainText,
    richText: {
      version: richText.version,
      parts: richText.parts.map((part) => {
        if (part.fontId === undefined || part.fontUrl !== undefined) {
          return part;
        }
        const fontUrl = resolveFontUrl(part.fontId);
        return fontUrl === null ? part : { ...part, fontUrl };
      }),
    },
  };
}

async function tryWriteClipboardItems(items: Record<string, Blob>): Promise<boolean> {
  if (navigator.clipboard === undefined || navigator.clipboard.write === undefined || typeof ClipboardItem === 'undefined') {
    return false;
  }
  try {
    await navigator.clipboard.write([new ClipboardItem(items)]);
    return true;
  } catch {
    return false;
  }
}

export async function writeClipboardPayload(payload: ClipboardWritePayload): Promise<void> {
  const plainText = payload.plainText;
  const richText = payload.richText;
  const html = richText === undefined ? null : buildClipboardHtml(richText);
  if (richText !== undefined && html !== null) {
    const richJson = JSON.stringify(richText);
    const fullWriteSucceeded = await tryWriteClipboardItems({
      'text/plain': new Blob([plainText], { type: 'text/plain' }),
      'text/html': new Blob([html], { type: 'text/html' }),
      [EFFINDOM_RICH_TEXT_CLIPBOARD_MIME]: new Blob([richJson], { type: EFFINDOM_RICH_TEXT_CLIPBOARD_MIME }),
    });
    if (fullWriteSucceeded) {
      return;
    }
    const htmlWriteSucceeded = await tryWriteClipboardItems({
      'text/plain': new Blob([plainText], { type: 'text/plain' }),
      'text/html': new Blob([html], { type: 'text/html' }),
    });
    if (htmlWriteSucceeded) {
      return;
    }
  }
  if (navigator.clipboard !== undefined && navigator.clipboard.writeText !== undefined) {
    await navigator.clipboard.writeText(plainText);
  }
}
