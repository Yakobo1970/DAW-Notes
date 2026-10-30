"""Sammelt alle übersetzbaren Texte aus Source/ und schreibt Resources/Languages/set_german.ini.

Erfasst werden tr("key", "Text") und getText("key", "Text" | juce::String::fromUTF8("Text")),
auch über mehrere Zeilen aneinandergereihte Literale. Texte aus Arrays (Bandnamen, Mix-Tipps)
und die About-Geschichte werden gesondert ausgelesen.

Aufruf:  python Tools/extract_texts.py            -> schreibt set_german.ini
         python Tools/extract_texts.py --check    -> meldet Schlüssel, die in en/fr fehlen
"""
import os, re, sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SRC = os.path.join(ROOT, 'Source')
LANG_DIR = os.path.join(ROOT, 'Resources', 'Languages')

LIT = r'"(?:[^"\\]|\\.)*"'
CALL = re.compile(r'\b(?:tr|getText)\(\s*"([a-z0-9_]+)"\s*,\s*(?:juce::String::fromUTF8\(\s*)?((?:' + LIT + r'\s*)+)', re.S)


def join_literals(chunk):
    parts = re.findall(LIT, chunk)
    return ''.join(p[1:-1] for p in parts).replace('\\"', '"')


def array_literals(text, name):
    m = re.search(r'\b' + name + r'\[[^\]]*\]\s*=\s*\{(.*?)\};', text, re.S)
    return [p[1:-1].replace('\\"', '"') for p in re.findall(LIT, m.group(1))] if m else []


def collect():
    texts, origin = {}, {}
    for f in sorted(os.listdir(SRC)):
        if not f.endswith(('.cpp', '.h')) or f.startswith(('sqlite3', 'shell')):
            continue
        content = open(os.path.join(SRC, f), encoding='utf-8').read()
        for key, chunk in CALL.findall(content):
            value = join_literals(chunk)
            if key in texts and texts[key] != value:
                print(f'WARNUNG: {key} unterschiedlich in {origin[key]} und {f}', file=sys.stderr)
            texts.setdefault(key, value)
            origin.setdefault(key, f)

        if f == 'GenreAnalysis.cpp':
            for i, t in enumerate(array_literals(content, 'bandAdviceLow')):
                texts[f'ga_band_low_{i}'] = t
            for i, t in enumerate(array_literals(content, 'bandAdviceHigh')):
                texts[f'ga_band_high_{i}'] = t
            for i, t in enumerate(array_literals(content, 'names')):
                texts[f'ga_band_name_{i}'] = t
        if f == 'AboutOverlay.cpp':
            m = re.search(r'defaultStory\s*=\s*((?:' + LIT + r'\s*)+);', content, re.S)
            if m:
                texts['about_story'] = join_literals(m.group(1))
        if f == 'GhostArrangement.cpp':
            for key, value in re.findall(r'\{\s*"(ghost_(?:el|sec)_[a-z]+)",\s*"((?:[^"\\]|\\.)*)"', content):
                texts[key] = value
    return texts


MOODS = """[Moods]
1=Energetisch
2=Melancholisch
3=Düster
4=Fröhlich
5=Entspannt
6=Aggressiv
7=Treibend
8=Atmosphärisch
9=Gefühlvoll
10=Hypnotisch
11=Nostalgisch
12=Euphorisierend
13=Dramatisch
14=Mystisch
15=Groovig
16=Ruhig
17=Monoton
18=Verspielt
"""

COLOURS = """[MoodColours]
1=#FF007F
2=#00FF66
3=#00E5FF
4=#FFD700
5=#FF4500
6=#A020F0
7=#FF1493
8=#00FF00
9=#00FFFF
10=#FF00FF
11=#39FF14
12=#FF3131
13=#F5F5F5
14=#00F5FF
15=#FF69B4
16=#FFF01F
17=#7B68EE
18=#FF8C00
"""


def write_ini(path, moods, ui, header):
    with open(path, 'w', encoding='utf-8', newline='\r\n') as out:
        out.write(header + '\n\n' + moods + '\n' + COLOURS + '\n[UI]\n')
        for key in sorted(ui):
            out.write(f'{key}={ui[key]}\n')


def read_ini(path):
    ui, section = {}, ''
    for line in open(path, encoding='utf-8-sig'):
        line = line.rstrip('\r\n')
        if line.startswith('['):
            section = line.strip('[]')
        elif '=' in line and section == 'UI' and not line.startswith(';'):
            k, v = line.split('=', 1)
            ui[k.strip()] = v
    return ui


if __name__ == '__main__':
    texts = collect()
    if '--check' in sys.argv:
        for lang in ('english', 'france'):
            path = os.path.join(LANG_DIR, f'set_{lang}.ini')
            have = read_ini(path) if os.path.exists(path) else {}
            missing = sorted(set(texts) - set(have))
            print(f'{lang}: {len(missing)} fehlend', *missing, sep='\n  ')
    else:
        os.makedirs(LANG_DIR, exist_ok=True)
        write_ini(os.path.join(LANG_DIR, 'set_german.ini'), MOODS, texts,
                  '; DAW Notes - Sprachdatei Deutsch\n; Wird bei der Installation als settings.ini nach %APPDATA%\\DAW Notes kopiert.\n'
                  '; Zeilenumbrüche in Texten als \\n schreiben, %s/%d sind Platzhalter.')
        print(len(texts), 'Texte geschrieben')
