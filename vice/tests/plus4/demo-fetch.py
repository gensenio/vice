#!/usr/bin/env python3
"""Download the quick TED demo corpus outside the checkout (stdlib only)."""
import argparse
from concurrent.futures import ThreadPoolExecutor
import hashlib
import json
from pathlib import Path
import re
import subprocess
import zipfile

NAMES = ['Rockstar_Ate_My_Border', 'HNY2013', 'Questionmark', 'Chaos',
         'Five_Magics', 'Rocket_Science', 'States_United', 'Metamerism',
         'Crackers_Demo_4', '8_Shades_of_Black', 'Dreamtime_2K17', 'Dreamtime_2K18']
BASE = 'https://plus4world.powweb.com'


def fetch(name, root):
    page = BASE + '/software/' + name
    html = subprocess.check_output(['curl', '-f', '-sS', '-L', '--max-time', '30', page]).decode('utf-8', errors='replace')
    (root / (name + '.html')).write_text(html)
    links = list(dict.fromkeys(re.findall(r'href="(/dl/[^"<>]+\.(?:zip|prg|d64))"', html)))
    if not links:
        raise RuntimeError('No download found: ' + page)
    url = BASE + links[0]
    data = subprocess.check_output(['curl', '-f', '-sS', '-L', '--max-time', '30', url])
    archive = root / (name + Path(links[0]).suffix)
    archive.write_bytes(data)
    folder = root / name
    folder.mkdir(exist_ok=True)
    if zipfile.is_zipfile(archive):
        with zipfile.ZipFile(archive) as z:
            for member in z.infolist():
                if member.is_dir():
                    continue
                target = (folder / member.filename).resolve()
                if not target.is_relative_to(folder.resolve()):
                    raise ValueError('Unsafe ZIP path')
                target.parent.mkdir(parents=True, exist_ok=True)
                target.write_bytes(z.read(member))
    else:
        (folder / archive.name).write_bytes(data)
    files = [dict(path=str(p.relative_to(root)), sha256=hashlib.sha256(p.read_bytes()).hexdigest())
             for p in sorted(folder.rglob('*')) if p.suffix.lower() in ('.prg', '.d64', '.d81')]
    print(name + ': ' + ', '.join(f['path'] for f in files), flush=True)
    return dict(id=name, page=page, download=url, archive_sha256=hashlib.sha256(data).hexdigest(), files=files)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('directory', type=Path)
    args = parser.parse_args()
    args.directory.mkdir(parents=True, exist_ok=True)
    with ThreadPoolExecutor(max_workers=4) as pool:
        rows = list(pool.map(lambda name: fetch(name, args.directory), NAMES))
    (args.directory / 'downloads.json').write_text(json.dumps(rows, indent=2) + '\n')
