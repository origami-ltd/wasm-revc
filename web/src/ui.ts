/** Page chrome: header, framed canvas stage, and the first-run ownership gate.
    Same components as the Generals shell — only data-brand="vice" and the copy differ. */

export const INSTALL_HELP = `
  <p>Point the picker at your own installed copy — the folder that contains
     <code class="text-signal">models/gta3.img</code> and
     <code class="text-signal">data/gta_vc.dat</code>.</p>
  <p>On Windows with Steam in its default place that is under
     <code class="text-signal break-all">C:\\Program Files (x86)\\Steam\\steamapps\\common\\</code>;
     Steam → Library → right-click the game → Manage → Browse local files opens it directly.</p>
  <p>A disc install works the same way — copy it to disk first, then point the browser at that
     folder. Nothing is uploaded and nothing is distributed from here.</p>`;

/**
 * Brazilian Portuguese
 * ====================
 *
 * The game reads whichever GXT matches its language setting, so a translation is a drop-in: put
 * the translated `american.gxt` in TEXT/ and the game is in Portuguese. It needs `models/fonts.txd`
 * from the same translation as well — the stock font has no accented glyphs and every "ç" and "õ"
 * comes out as a box.
 *
 * The widescreen splash/loading TXDs are the default; the translation also ships a 4:3 set.
 *
 * Verified with the MixMods pt-BR translation: MENU PRINCIPAL / COMEÇAR / OPÇÕES / SAIR, accents
 * intact.
 */
