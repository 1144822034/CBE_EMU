/**
 * Firmware LKV2 GIF decoder — port of src/gifDecode.c gifDecodeExt.
 * Input is the LZSS-decoded payload (not the on-disk wrapper).
 */

export type GifRgba = {
  width: number;
  height: number;
  /** RGBA8888, row stride = width (no padding). Transparent when RGB565 was 0. */
  rgba: Uint8ClampedArray;
  /** Original RGB565 with pitch padding (for map blit). */
  rgb565: Uint16Array;
  pitch: number;
  /** `raster` = plain PNG/JPEG (PixiJS redesign sheets); default LKV2. */
  sourceKind?: "lkv2" | "raster";
};

/** PixiJS-only: accept stock PNG/JPEG/WebP (and similar) without LKV2 wrap. */
export function isRasterImageMagic(data: Uint8Array): boolean {
  if (data.length >= 8 && data[0] === 0x89 && data[1] === 0x50 && data[2] === 0x4e && data[3] === 0x47) {
    return true; // PNG
  }
  if (data.length >= 3 && data[0] === 0xff && data[1] === 0xd8 && data[2] === 0xff) {
    return true; // JPEG
  }
  if (
    data.length >= 12 &&
    data[0] === 0x52 &&
    data[1] === 0x49 &&
    data[2] === 0x46 &&
    data[3] === 0x46 &&
    data[8] === 0x57 &&
    data[9] === 0x45 &&
    data[10] === 0x42 &&
    data[11] === 0x50
  ) {
    return true; // RIFF WEBP
  }
  return false;
}

function packRgb565(r: number, g: number, b: number): number {
  return ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3);
}

/** Build GifRgba from RGBA8888; near-black optional key → transparent. */
export function rgbaToGifRgba(
  width: number,
  height: number,
  rgba: Uint8ClampedArray,
  opts?: { keyNearBlack?: number; sourceKind?: "lkv2" | "raster" },
): GifRgba {
  const key = opts?.keyNearBlack ?? 0;
  const out = new Uint8ClampedArray(width * height * 4);
  const pad = (4 - (width & 3)) & 3;
  const pitch = width + pad;
  const rgb565 = new Uint16Array(pitch * height);
  for (let y = 0; y < height; y++) {
    for (let x = 0; x < width; x++) {
      const i = (y * width + x) * 4;
      let r = rgba[i]!;
      let g = rgba[i + 1]!;
      let b = rgba[i + 2]!;
      let a = rgba[i + 3]!;
      if (key > 0 && a > 0 && r + g + b < key) a = 0;
      out[i] = r;
      out[i + 1] = g;
      out[i + 2] = b;
      out[i + 3] = a;
      rgb565[y * pitch + x] = a < 16 ? 0 : packRgb565(r, g, b);
    }
  }
  return {
    width,
    height,
    rgba: out,
    rgb565,
    pitch,
    sourceKind: opts?.sourceKind ?? "lkv2",
  };
}

/**
 * Decode browser-decodable raster bytes (PNG/JPEG/WebP) into GifRgba.
 * Used by PixiJS when art is dropped in as a normal image instead of LKV2.
 */
export async function decodeRasterImageBytes(
  data: Uint8Array,
  opts?: { keyNearBlack?: number },
): Promise<GifRgba> {
  const copy = new Uint8Array(data.byteLength);
  copy.set(data);
  const blob = new Blob([copy]);
  const bmp = await createImageBitmap(blob);
  try {
    const canvas = document.createElement("canvas");
    canvas.width = bmp.width;
    canvas.height = bmp.height;
    const ctx = canvas.getContext("2d");
    if (!ctx) throw new Error("gif: no 2d context for raster decode");
    ctx.drawImage(bmp, 0, 0);
    const img = ctx.getImageData(0, 0, bmp.width, bmp.height);
    return rgbaToGifRgba(
      bmp.width,
      bmp.height,
      new Uint8ClampedArray(img.data),
      { ...opts, sourceKind: "raster" },
    );
  } finally {
    bmp.close();
  }
}

class LzwReader {
  private gif: Uint8Array;
  private pos: number;
  private blkRemaining = 0;
  private byteIdx = -1;
  private win0 = 0;
  private win1 = 0;
  private win2 = 0;
  curBit = -1;
  curCodeSize = 0;
  curMaxCode = 0;

  constructor(gif: Uint8Array, start: number) {
    this.gif = gif;
    this.pos = start;
  }

  get position(): number {
    return this.pos;
  }

  setPosition(p: number): void {
    this.pos = p;
  }

  readCode(): number {
    let result = -1;
    if (this.curBit === -1) {
      this.blkRemaining = 0;
      this.byteIdx = -1;
    }
    const oldByteIdx = this.byteIdx;
    this.curBit += this.curCodeSize;
    const newByteIdx = (this.curBit / 8) | 0;
    this.byteIdx = newByteIdx;
    let bytesToRead = newByteIdx - oldByteIdx;
    const bitRem = this.curBit % 8;
    const shift = bitRem + 17 - this.curCodeSize;
    const mask = this.curMaxCode - 1;

    while (bytesToRead > 0) {
      if (this.blkRemaining === 0) {
        if (this.pos >= this.gif.length) return result;
        this.blkRemaining = this.gif[this.pos++]!;
        if (this.blkRemaining === 0) return result;
      }
      this.win0 = this.win1;
      this.win1 = this.win2;
      this.win2 = this.gif[this.pos++]!;
      this.blkRemaining--;
      bytesToRead--;
    }

    const raw = (this.win2 << 16) | (this.win1 << 8) | this.win0;
    result = (raw >> shift) & mask;
    return result;
  }
}

function rgb565ToRgbaPixel(pixel: number, out: Uint8ClampedArray, i: number): void {
  const r = (((pixel >> 11) & 0x1f) * 255) / 31;
  const g = (((pixel >> 5) & 0x3f) * 255) / 63;
  const b = ((pixel & 0x1f) * 255) / 31;
  out[i] = r;
  out[i + 1] = g;
  out[i + 2] = b;
  out[i + 3] = pixel === 0 ? 0 : 255;
}

export function decodeLkv2Gif(data: Uint8Array): GifRgba {
  if (data.length < 16) throw new Error("gif: truncated");
  const dictSize =
    ((data[0]! << 24) | (data[1]! << 16) | (data[2]! << 8) | data[3]!) >>> 0;
  if (dictSize <= 0 || dictSize > 1024 * 1024) {
    throw new Error(`gif: bad dict_size=${dictSize}`);
  }
  const flags = data[4]!;
  if ((flags & 0x80) === 0) {
    throw new Error(`gif: missing local palette flags=${flags.toString(16)}`);
  }

  const colorCount = 1 << ((flags & 7) + 1);
  const maxRootCode = colorCount - 1;
  if (colorCount <= 0 || colorCount > 256 || colorCount > dictSize) {
    throw new Error(`gif: bad color_count=${colorCount}`);
  }

  let ptr = 5;
  // Firmware: src_ptr = ptr+2, loop while ptr[1]==0 — first entry uses palette at ptr+2.
  if (data[ptr + 1] !== 0) {
    throw new Error("gif: unexpected control byte");
  }

  const palette = new Uint16Array(256);
  const srcPtr = ptr + 2;
  for (let i = 0; i < colorCount; i++) {
    const lo = data[srcPtr + i * 2]!;
    const hi = data[srcPtr + i * 2 + 1]!;
    // Stored as BE RGB565 pairs; firmware swaps to host LE.
    const be = (hi << 8) | lo;
    palette[i] = ((be & 0xff) << 8) | ((be >> 8) & 0xff);
  }

  const tmp = srcPtr + 2 * colorCount;
  let transIdx = 256;
  // Firmware: GIFArray = tmp+3, then always GIFArray = tmp+6; sep = GIFArray[2].
  if ((data[tmp + 3]! & 1) !== 0) {
    transIdx = data[tmp + 6]!;
  }
  void transIdx;

  let gifPos = tmp + 6;
  const separator = data[gifPos + 2]!;
  gifPos += 3;
  if (separator !== 0x2c) {
    throw new Error(`gif: no image separator (${separator})`);
  }

  // Image descriptor follows 0x2C: left(2) top(2) width(2) height(2) packed LZW
  // Firmware reads width/height at GIFArray+4 after the +3 advance past sep probe.
  const dim = gifPos + 4;
  const imgWidth = data[dim]! | (data[dim + 1]! << 8);
  const imgHeight = data[dim + 2]! | (data[dim + 3]! << 8);
  if (imgWidth <= 0 || imgHeight <= 0 || imgWidth > 1024 || imgHeight > 1024) {
    throw new Error(`gif: bad size ${imgWidth}x${imgHeight}`);
  }
  gifPos = dim + 4;
  const packed = data[gifPos++]!;
  if ((packed >> 6) !== 0) {
    throw new Error(`gif: unsupported packed=${packed.toString(16)}`);
  }
  const codeSize = data[gifPos++]!;
  if (codeSize <= 0 || codeSize > 8) {
    throw new Error(`gif: bad code_size=${codeSize}`);
  }

  const pad = (4 - (imgWidth & 3)) & 3;
  const pitch = imgWidth + pad;
  const rgb565 = new Uint16Array(pitch * imgHeight);

  let curCodeSize = codeSize + 1;
  let curMaxCode = 1 << (codeSize + 1);
  const clearCode = 1 << codeSize;
  const endCode = clearCode + 1;
  let nextCode = clearCode + 2;
  if (nextCode >= dictSize) throw new Error("gif: dict too small");

  const prefix = new Int32Array(dictSize);
  const suffix = new Uint8Array(dictSize);
  const outCode = new Uint8Array(dictSize);

  const reader = new LzwReader(data, gifPos);
  reader.curCodeSize = curCodeSize;
  reader.curMaxCode = curMaxCode;
  reader.curBit = -1;

  let pixelPos = 0;
  let totalPixels = imgWidth * imgHeight;
  let firstChar = 0;
  let prevCode = 0;
  let stackIdx = 0;

  const writePixel = (index: number): void => {
    rgb565[pixelPos++] = palette[index]!;
    totalPixels--;
    if (totalPixels % imgWidth === 0) {
      pixelPos += pad;
    }
  };

  while (true) {
    let curCode = reader.readCode();
    if (totalPixels <= 0) break;

    if (curCode === clearCode) {
      curCodeSize = codeSize + 1;
      curMaxCode = 1 << (codeSize + 1);
      nextCode = clearCode + 2;
      reader.curCodeSize = curCodeSize;
      reader.curMaxCode = curMaxCode;

      prevCode = reader.readCode();
      if (prevCode < 0 || prevCode === endCode) break;
      if (prevCode > maxRootCode) {
        throw new Error(`gif: bad prev_code=${prevCode}`);
      }
      firstChar = prevCode & 0xff;
      writePixel(firstChar);
    } else {
      if (curCode < 0 || curCode === endCode) break;
      const savedCode = curCode;
      stackIdx = 0;

      if (curCode >= nextCode) {
        if (prevCode < 0) throw new Error("gif: KwKwK without prev");
        curCode = prevCode;
        outCode[0] = firstChar;
        stackIdx = 1;
      }

      while (curCode > maxRootCode) {
        if (
          curCode < 0 ||
          curCode >= dictSize ||
          curCode >= nextCode ||
          stackIdx >= dictSize
        ) {
          throw new Error("gif: bad prefix walk");
        }
        outCode[stackIdx++] = suffix[curCode]!;
        curCode = prefix[curCode]!;
      }
      if (curCode < 0 || curCode > maxRootCode || stackIdx >= dictSize) {
        throw new Error("gif: bad root");
      }

      firstChar = curCode & 0xff;
      outCode[stackIdx] = firstChar;

      while (stackIdx >= 0 && totalPixels > 0) {
        writePixel(outCode[stackIdx]!);
        stackIdx--;
      }

      if (nextCode < dictSize) {
        prefix[nextCode] = prevCode;
        suffix[nextCode] = firstChar;
      }
      stackIdx = 0;
      prevCode = savedCode;
      if (nextCode < dictSize) nextCode++;
      if (nextCode >= curMaxCode && curCodeSize < 12) {
        curMaxCode *= 2;
        curCodeSize++;
        reader.curCodeSize = curCodeSize;
        reader.curMaxCode = curMaxCode;
      }
    }
  }

  const rgba = new Uint8ClampedArray(imgWidth * imgHeight * 4);
  for (let y = 0; y < imgHeight; y++) {
    for (let x = 0; x < imgWidth; x++) {
      const px = rgb565[y * pitch + x]!;
      rgb565ToRgbaPixel(px, rgba, (y * imgWidth + x) * 4);
    }
  }

  return { width: imgWidth, height: imgHeight, rgba, rgb565, pitch, sourceKind: "lkv2" };
}
