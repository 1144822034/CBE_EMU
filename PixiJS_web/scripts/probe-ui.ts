import fs from 'node:fs';
import path from 'node:path';
import { unwrapResource } from '../src/resources/lzss.ts';
import { decodeLkv2Gif } from '../src/resources/lkv2Gif.ts';
const root = path.resolve('../web/fs/JHOnlineData');
const names = ['Background.gif','loading.gif','title.gif','title2.gif','title001.gif','ui_hpsp.gif','ui_bag.gif','UI_menu2.gif','ui_chatlog.gif','autobattle_button.gif','ui_offline.gif'];
for (const name of names) {
  try {
    const g = decodeLkv2Gif(unwrapResource(fs.readFileSync(path.join(root, name))));
    console.log(name, g.width+'x'+g.height);
  } catch (e) { console.log(name, String(e.message||e)); }
}
