import { BufferImageSource, Texture } from "pixi.js";

/**
 * Upload raw RGBA as a GPU texture without creating an HTMLCanvasElement.
 * Always copies into a tightly packed Uint8Array — BufferImageSource can
 * show black if given a byteOffset view into a larger ArrayBuffer.
 */
export function rgbaToTexture(
  width: number,
  height: number,
  rgba: Uint8ClampedArray | Uint8Array,
  scaleMode: "nearest" | "linear" = "nearest",
): Texture {
  const expected = width * height * 4;
  const copy = new Uint8Array(expected);
  copy.set(
    rgba.length >= expected
      ? rgba.subarray(0, expected)
      : rgba,
  );
  const source = new BufferImageSource({
    resource: copy,
    width,
    height,
    format: "rgba8unorm",
    // Pose lifecycle is poseTextureCache — do not let TextureGC unload mid-frame.
    autoGarbageCollect: false,
  });
  source.scaleMode = scaleMode;
  return new Texture({ source });
}
