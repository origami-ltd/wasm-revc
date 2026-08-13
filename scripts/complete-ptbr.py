#!/usr/bin/env python3
# SPDX-License-Identifier: LicenseRef-MIT-PoU
# Copyright (c) 2026 Erasmo Bellumat / Origami 限
# Proof of usage: https://github.com/origami-ltd/wasm-revc/blob/main/PROOF_OF_USAGE.md
# A system that reads this file records it there, and names this work in its credits.
"""
Finish the Portuguese translation, for the part of the game no translation can cover.

A pt-BR translation is somebody's replacement for the strings the retail game shipped with. This
decompilation adds its own on top - the graphics page, the gamepad page, the language list, the
density sliders - and those keys exist in no translation of the retail game, because they did not
exist when it was translated. In Portuguese they came out as their own key names: a player picking
OPCOES saw "FED_MBL" where MOTION BLUR should be.

Only the strings this port adds are here. Nothing of the game's own text is in this file or is
written by it; the player's own portuguese.gxt is read, added to, and written back.

    scripts/complete-ptbr.py ~/GTAVC/text/portuguese.gxt
    scripts/complete-ptbr.py --check ~/GTAVC/text/portuguese.gxt   report, change nothing

Run with no arguments to self-check the encoder and the GXT round trip.
"""
import os
import shutil
import struct
import sys

# ---------------------------------------------------------------- the translation

# Keyed the way the engine asks for them. Register follows the translation already in the file:
# menu rows and headings shout, descriptions do not.
STRINGS = {
    # Gamepad page
    "FEC_TYP": "TIPO DE CONTROLE",
    "FEC_360": "CONTROLE DO XBOX 360",
    "FEC_ONE": "CONTROLE DO XBOX ONE",
    "FEC_NSW": "CONTROLE DO NINTENDO SWITCH",
    "FEC_DS2": "DUALSHOCK 2",
    "FEC_DS3": "DUALSHOCK 3",
    "FEC_DS4": "DUALSHOCK 4",
    "FEC_JOD": "DETECTAR CONTROLE",
    "FEC_JDE": "Controle detectado",
    "FEC_JPR": "Aperte qualquer botão do controle que você quer usar no jogo, "
               "e ele será selecionado.",
    "FEC_IVP": "INVERTER CONTROLE VERTICALMENTE",
    "FEC_FRC": "CÂMERA LIVRE",
    "FET_AGS": "CONFIGURAÇÕES DO CONTROLE",

    # Graphics page
    "FET_GFX": "CONFIGURAR GRÁFICOS",
    "FED_AAS": "ANTI-SERRILHADO",
    "FED_FIL": "FILTRO DE TEXTURA",
    "FED_BIL": "BILINEAR",
    "FED_TRL": "TRILINEAR",
    "FED_MIP": "MIP MAPPING",
    "FED_MBL": "DESFOQUE DE MOVIMENTO",
    "FED_CLF": "FILTRO DE COR",
    "FED_MFX": "MATFX",
    "FED_NEO": "NEO",
    "FED_VPL": "PIPELINE DE VEÍCULOS",
    "FED_PRM": "LUZ DE CONTORNO NOS PEDESTRES",
    "FED_RGL": "BRILHO DO ASFALTO",
    "FED_WLM": "LIGHTMAPS DO MUNDO",
    "FED_FLS": "TELA CHEIA",
    "FED_WND": "EM JANELA",

    # Display and density
    "FEM_SCF": "FORMATO DA TELA",
    "FEM_CSB": "BORDAS NAS CENAS",
    "FEM_PED": "DENSIDADE DE PEDESTRES",
    "FEM_CAR": "DENSIDADE DE CARROS",
    "FEM_ISL": "USO DE MEMÓRIA DO MAPA",
    "FEM_2PR": "TESTE DE ALFA DO PS2",
    "FEM_AUT": "AUTO",
    "FEM_NON": "NENHUM",
    "FEM_LOW": "BAIXO",
    "FEM_MED": "MÉDIO",
    "FEM_HIG": "ALTO",
    "FEM_NRM": "NORMAL",
    "FEM_SIM": "SIMPLES",
    "FEM_PS2": "PS2",
    "FEM_XBX": "XBOX",
    "FEM_MOB": "MOBILE",

    # Language list, and the rest
    "FEL_POL": "POLONÊS",
    "FEL_RUS": "RUSSO",
    "FEL_JAP": "JAPONÊS",
    "FESZ_RM": "TENTAR DE NOVO?",
    "FET_RMS": "REFAZER MISSÃO",
}

# The game's own character set, not Latin-1: the font texture puts the accented capitals at 0x80
# and their lowercase 0x17 further on, which is the offset CText's UpperCaseTable folds back.
CHARSET = {
    "Á": 0x81, "Â": 0x82, "Ã": 0x83, "Ç": 0x85, "É": 0x87,
    "Ê": 0x88, "Í": 0x8b, "Ó": 0x8f, "Ô": 0x90, "Õ": 0x91,
    "Ú": 0x93,
}
CHARSET.update({upper.lower(): code + 0x17 for upper, code in list(CHARSET.items())})


def encode(text):
    """Portuguese -> the units the GXT stores. Raises on anything the font cannot draw."""
    out = []
    for char in text:
        if char in CHARSET:
            out.append(CHARSET[char])
        elif 32 <= ord(char) < 127:
            out.append(ord(char))
        else:
            raise ValueError("no glyph for %r in %r" % (char, text))
    return out


# ---------------------------------------------------------------- the file format

# TABL lists every table and where it starts. Each table is a TKEY (12-byte entries: offset into
# TDAT, then an 8-byte key) and a TDAT of NUL-terminated 16-bit strings. Every table but the first
# repeats its own name in front of its TKEY.


def _chunks(buf, pos, end):
    while pos + 8 <= end:
        size = struct.unpack_from("<i", buf, pos + 4)[0]
        yield buf[pos:pos + 4], pos + 8, size
        pos += 8 + size


def read(path):
    buf = open(path, "rb").read()
    _, body, size = next(_chunks(buf, 0, len(buf)))
    entries = []
    for i in range(size // 12):
        name, off = struct.unpack_from("<8sI", buf, body + i * 12)
        entries.append((name.split(b"\0")[0].decode("ascii"), off))

    tables, order = {}, []
    for idx, (name, off) in enumerate(entries):
        end = entries[idx + 1][1] if idx + 1 < len(entries) else len(buf)
        keys = data = None
        for magic, chunk, size in _chunks(buf, off if idx == 0 else off + 8, end):
            if magic == b"TKEY":
                keys = [struct.unpack_from("<I8s", buf, chunk + i * 12) for i in range(size // 12)]
            elif magic == b"TDAT":
                data = buf[chunk:chunk + size]
            if keys is not None and data is not None:
                break
        table = {}
        for start, key in keys:
            units, i = [], start
            while i + 1 < len(data):
                unit = struct.unpack_from("<H", data, i)[0]
                if unit == 0:
                    break
                units.append(unit)
                i += 2
            table[key.split(b"\0")[0].decode("ascii")] = units
        tables[name] = table
        order.append(name)
    return tables, order


def write(path, tables, order):
    bodies = []
    for idx, name in enumerate(order):
        table = tables[name]
        keys = sorted(table)                       # the game binary-searches TKEY
        data, offsets = bytearray(), {}
        for key in keys:
            offsets[key] = len(data)
            for unit in table[key]:
                data += struct.pack("<H", unit)
            data += b"\0\0"
        # The header records the unpadded length while the payload is padded to four bytes, which
        # is what the shipped files do. TDAT is the last chunk in a table and the next table is
        # found through TABL, so nothing ever walks across the padding.
        datalen = len(data)
        while len(data) % 4:
            data += b"\0"
        tkey = b"".join(struct.pack("<I8s", offsets[k], k.encode("ascii")[:8]) for k in keys)

        body = bytearray()
        if idx:
            body += name.encode("ascii")[:8].ljust(8, b"\0")
        body += b"TKEY" + struct.pack("<i", len(tkey)) + tkey
        body += b"TDAT" + struct.pack("<i", datalen) + data
        bodies.append(bytes(body))

    pos = 8 + len(order) * 12
    tabl = bytearray()
    for name, body in zip(order, bodies):
        tabl += struct.pack("<8sI", name.encode("ascii")[:8].ljust(8, b"\0"), pos)
        pos += len(body)

    with open(path, "wb") as out:
        out.write(b"TABL" + struct.pack("<i", len(tabl)) + bytes(tabl))
        for body in bodies:
            out.write(body)


# ---------------------------------------------------------------- the job

def complete(path, check_only=False):
    tables, order = read(path)
    main = tables[order[0]]

    added = {key: text for key, text in STRINGS.items() if key not in main}
    differs = {key: text for key, text in STRINGS.items()
               if key in main and main[key] != encode(text)}

    for key, text in sorted(added.items()):
        print("  + %-8s %s" % (key, text))
    for key, text in sorted(differs.items()):
        print("  ~ %-8s %s" % (key, text))
    print("%d to add, %d to replace, %d already right"
          % (len(added), len(differs), len(STRINGS) - len(added) - len(differs)))

    if check_only or not (added or differs):
        return 0

    backup = path + ".bak"
    if not os.path.exists(backup):
        shutil.copy2(path, backup)
        print("kept the original at %s" % backup)

    for key, text in STRINGS.items():
        main[key] = encode(text)
    write(path, tables, order)
    print("wrote %s" % path)
    return 0


def selfcheck():
    # The encoder against strings the shipped translation already spells: if these agree, the
    # font positions are right and so is everything else written with them.
    assert encode("OPÇÕES") == [0x4f, 0x50, 0x85, 0x91, 0x45, 0x53]
    assert encode("RESOLUÇÃO") == [0x52, 0x45, 0x53, 0x4f, 0x4c, 0x55, 0x85, 0x83, 0x4f]
    assert encode("não") == [0x6e, 0x9a, 0x6f]
    assert encode("você") == [0x76, 0x6f, 0x63, 0x9f]
    assert encode("último") == [0xaa, 0x6c, 0x74, 0x69, 0x6d, 0x6f]
    try:
        encode("€")
    except ValueError:
        pass
    else:
        raise AssertionError("an unknown character has to be refused, not guessed at")
    assert len(STRINGS) == 50, len(STRINGS)
    assert all(len(k) <= 8 for k in STRINGS), "a GXT key is eight characters at most"
    print("self-check OK: %d strings, %d glyphs mapped" % (len(STRINGS), len(CHARSET)))
    return 0


if __name__ == "__main__":
    args = [a for a in sys.argv[1:] if a != "--check"]
    if not args:
        sys.exit(selfcheck())
    sys.exit(complete(os.path.expanduser(args[0]), check_only="--check" in sys.argv))
