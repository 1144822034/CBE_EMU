import fs from "node:fs";

const buf = fs.readFileSync("../bin/font_gb.uc3");
const dv = new DataView(buf.buffer, buf.byteOffset, buf.byteLength);
console.log("size", buf.length);
console.log("w0", dv.getInt32(0, true), "w", dv.getInt32(4, true), "h", dv.getInt32(8, true), "count", dv.getInt32(12, true));
const count = dv.getInt32(12, true);
const start = 16;
for (let i = 0; i < 8; i++) {
  const gb = dv.getUint16(start + i * 6, true);
  const off = dv.getUint32(start + i * 6 + 2, true);
  console.log(i, "gb=0x" + gb.toString(16), "off", off);
}
function find(code: number, label: string) {
  for (let i = 0; i < count; i++) {
    const gb = dv.getUint16(start + i * 6, true);
    if (gb === code) {
      console.log("found", label, "0x" + code.toString(16), "off", dv.getUint32(start + i * 6 + 2, true));
      return;
    }
  }
  console.log("missing", label, "0x" + code.toString(16));
}
const huang = Buffer.from("黄", "gbk").readUInt16LE(0);
const rong = Buffer.from("蓉", "gbk").readUInt16LE(0);
find(huang, "黄");
find(rong, "蓉");
find("A".charCodeAt(0) << 8, "A");
find("0".charCodeAt(0) << 8, "0");
