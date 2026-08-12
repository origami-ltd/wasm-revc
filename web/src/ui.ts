/** Page chrome: header, framed canvas stage, and the first-run ownership gate.
    Same components as the Generals shell — only data-brand="vice" and the copy differ. */

export const INSTALL_HELP = `
  <p>If Steam is installed in the default location on drive C:</p>
  <p><code class="text-signal break-all">C:\\Program Files (x86)\\Steam\\steamapps\\common\\Grand Theft Auto Vice City\\</code></p>
  <p>To open the folder directly: Steam → Library → right-click <strong>Grand Theft Auto: Vice City</strong>
     → Manage → Browse local files.</p>
  <p>On macOS or Linux, pick the folder that contains
     <code class="text-signal">models/gta3.img</code> and
     <code class="text-signal">data/gta_vc.dat</code>. A disc install works the same way —
     copy it to disk first, then point the browser at that folder.</p>`;

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
