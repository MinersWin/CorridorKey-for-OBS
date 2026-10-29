# SPDX-License-Identifier: GPL-3.0-or-later
"""Assemble weight-free packages from an installed CMake stage (Python 3.12+)."""
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import subprocess
import tarfile
import urllib.request
import zipfile

ROOT = Path(__file__).resolve().parents[1]
LIMIT = 2 * 1024**3
FORBIDDEN = {'.onnx', '.weights', '.safetensors', '.pt', '.pth', '.npz', '.engine', '.plan'}


def digest(path):
    checksum = hashlib.sha256()
    with path.open('rb') as stream:
        while block := stream.read(1024 * 1024):
            checksum.update(block)
    return checksum.hexdigest()


def write_json(path, value):
    path.write_text(json.dumps(value, indent=2) + '\n', encoding='utf-8')


def download(artifact, cache):
    cache.mkdir(parents=True, exist_ok=True)
    path = cache / artifact['sha256']
    if not path.exists():
        partial = path.with_suffix('.part')
        with urllib.request.urlopen(artifact['url'], timeout=60) as response, partial.open('wb') as out:
            shutil.copyfileobj(response, out)
        if partial.stat().st_size != artifact['bytes'] or digest(partial) != artifact['sha256']:
            raise ValueError('Runtime archive hash/size mismatch')
        partial.replace(path)
    if path.stat().st_size != artifact['bytes'] or digest(path) != artifact['sha256']:
        raise ValueError('Cached runtime archive hash/size mismatch')
    return path


def runtimes(stage, platform, cache):
    lock = json.loads((ROOT / 'packaging/runtime-lock.json').read_text())
    bundle = lock['bundles'][platform]
    data = stage / ('corridorkey/data' if platform == 'windows-x64' else 'usr/share/obs/obs-plugins/corridorkey')
    dest = data / 'runtimes' / platform / bundle['id']
    dest.mkdir(parents=True, exist_ok=True)
    components = []
    for key in bundle['artifacts']:
        artifact = lock['artifacts'][key]
        archive = download(artifact, cache)
        if artifact['url'].endswith('.tgz'):
            with tarfile.open(archive) as source:
                for member, target in artifact['files'].items():
                    info = source.getmember(member)
                    if not info.isfile():
                        raise ValueError(f'Expected a regular archive member: {member}')
                    path = dest / target
                    path.parent.mkdir(parents=True, exist_ok=True)
                    with source.extractfile(info) as inp, path.open('wb') as out:
                        shutil.copyfileobj(inp, out)
        else:
            with zipfile.ZipFile(archive) as source:
                for member, target in artifact['files'].items():
                    path = dest / target
                    path.parent.mkdir(parents=True, exist_ok=True)
                    with source.open(member) as inp, path.open('wb') as out:
                        shutil.copyfileobj(inp, out)
        components.append(dict(id=key, source=artifact['url'], archive_sha256=artifact['sha256'],
                               license=artifact['license'], files=list(artifact['files'].values())))
    descriptor = dict(bundle['descriptor'], id=bundle['id'])
    payload = sorted({name for component in components for name in component['files']})
    descriptor['files'] = {name: digest(dest / name) for name in payload}
    write_json(dest / (descriptor['backend'] + '.json'), descriptor)
    write_json(dest / 'provenance.json', components)
    write_json(data / 'runtimes/probes.json', {
        'schema_version': 1,
        'bundles': {descriptor['backend']: f"{platform}/{bundle['id']}/{descriptor['backend']}.json"}
    })
    print(f"{bundle['id']}: {sum(p.stat().st_size for p in dest.rglob('*') if p.is_file()):,} bytes")


def probe_fixtures(data):
    # Four authored constants, no learned parameters, external data or CK graph.
    # These tiny Add graphs are only for the disposable provider diagnostic.
    import onnx
    from onnx import TensorProto, helper
    tools = data / 'tools'
    tools.mkdir(parents=True, exist_ok=True)
    result = {}
    for name, dtype in [('fp32', TensorProto.FLOAT), ('fp16', TensorProto.FLOAT16)]:
        shape = [1, 1, 2, 2]
        graph = helper.make_graph([helper.make_node('Add', ['input', 'bias'], ['output'])],
                                  'corridorkey-synthetic-probe',
                                  [helper.make_tensor_value_info('input', dtype, shape)],
                                  [helper.make_tensor_value_info('output', dtype, shape)],
                                  [helper.make_tensor('bias', dtype, shape, [0, .25, .5, 1])])
        model = helper.make_model(graph, producer_name='corridorkey-packaging', opset_imports=[helper.make_opsetid('', 18)])
        model.ir_version = 8
        onnx.checker.check_model(model)
        path = tools / f'probe-{name}.onnx'
        path.write_bytes(model.SerializeToString(deterministic=True))
        result[path] = digest(path)
    return result


def inventory(stage, platform, synthetic_probes):
    data = stage / ('corridorkey/data' if platform == 'windows-x64' else 'usr/share/obs/obs-plugins/corridorkey')
    # Root build stages the project's static dependency list, including unbundled models.
    # Keep that reference distinct from the inventory of this actual binary package.
    static = data / 'licenses/THIRD_PARTY_NOTICES.md'
    if static.exists():
        static.replace(static.with_name('PROJECT_DEPENDENCIES.md'))
    for path in stage.rglob('*.pdb'):
        path.unlink()
    files = []
    for path in sorted(stage.rglob('*')):
        if path.is_symlink():
            raise ValueError(f'Unexpected staged symlink: {path}')
        if not path.is_file():
            continue
        if path in (data / 'BUNDLE_CONTENTS.json', data / 'THIRD_PARTY_NOTICES.md'):
            continue
        if path in synthetic_probes and digest(path) == synthetic_probes[path]:
            pass
        elif path.suffix.lower() in FORBIDDEN or '.weights.' in path.name:
            raise ValueError(f'Model/engine artifact in package: {path}')
        if path.stat().st_size >= LIMIT:
            raise ValueError(f'Individual file exceeds release limit: {path}')
        files.append(dict(path=path.relative_to(stage).as_posix(), bytes=path.stat().st_size, sha256=digest(path)))
    provenance = []
    for path in sorted(data.glob('runtimes/**/provenance.json')):
        provenance.extend(json.loads(path.read_text()))
    build = json.loads((data / 'packaging-build.json').read_text())
    write_json(data / 'BUNDLE_CONTENTS.json', dict(schema=1, platform=platform, components=provenance, files=files,
               build=build,
               synthetic_probes={p.relative_to(stage).as_posix(): sha for p, sha in synthetic_probes.items()}))
    notices = ['# Third-party notices for this package', '',
               'No neural model weights, CK graphs or compiled model engines are included.',
               'tools/probe-*.onnx are synthetic Add diagnostics with four authored constants.', '',
               'The plugin is GPL-3.0-or-later with the GPU runtime permission in licenses/LICENSE-EXCEPTION.md.', '',
               'OBS and Qt are supplied by the host. Linux system libraries are package dependencies.', '',
               '## Bundled runtime components', '']
    for component in provenance:
        notices += [f"- {component['id']}: {component['license']}. Source: {component['source']}"]
    notices += ['', '## Linked dependencies', '',
                f"- zlib {build['zlib']} (zlib licence), statically linked on Windows; system dependency on Linux.",
                f"- nlohmann/json {build['nlohmann_json']} (MIT), compiled into the native code.",
                f"- Host Qt {build['qt']} was used to build this package; Qt is not bundled."]
    notices += ['', '## Licence and notice files', '']
    for entry in files:
        name = entry['path'].lower()
        if any(s in name for s in ('license', 'notice', 'copyright')):
            notices.append(f"- `{entry['path']}` (SHA-256 `{entry['sha256']}`)")
    notices += ['', 'BUNDLE_CONTENTS.json records the payload paths, sizes, hashes and runtime provenance.', '']
    (data / 'THIRD_PARTY_NOTICES.md').write_text('\n'.join(notices), encoding='utf-8')


def windows(stage, out, version):
    root = stage / 'corridorkey'
    if not (root / 'corridorkey.dll').is_file() or not (root / 'data/tools/corridorkey_backend_probe.exe').is_file():
        raise ValueError('The Windows stage must contain the modern plugin and native backend helper')
    for layout in ('modern', 'legacy'):
        with zipfile.ZipFile(out / f'corridorkey-{version}-windows-x64-{layout}.zip', 'w', zipfile.ZIP_DEFLATED) as archive:
            for path in sorted(root.rglob('*')):
                if path.is_file() and path.suffix != '.pdb':
                    relative = path.relative_to(root)
                    if layout == 'legacy' and relative == Path('corridorkey.dll'):
                        relative = Path('bin/64bit/corridorkey.dll')
                    target = (Path('corridorkey') / relative).as_posix()
                    if layout == 'legacy' and relative == Path('data/BUNDLE_CONTENTS.json'):
                        content = json.loads(path.read_text())
                        for entry in content['files']:
                            if entry['path'] == 'corridorkey/corridorkey.dll':
                                entry['path'] = 'corridorkey/bin/64bit/corridorkey.dll'
                        archive.writestr(target, json.dumps(content, indent=2) + '\n')
                    else:
                        archive.write(path, target)


def linux(stage, out, version, distro):
    module = stage / 'usr/lib/x86_64-linux-gnu/obs-plugins/corridorkey.so'
    helper = stage / 'usr/libexec/corridorkey/corridorkey_backend_probe'
    if not module.is_file() or not helper.is_file():
        raise ValueError('Missing distro-layout module or executable helper')
    # Never erase executable bits on the probe (W00 did this with a blanket chmod).
    for path in stage.rglob('*'):
        path.chmod(0o755 if path.is_dir() or path == helper or path.name == 'corridorkey-runtime' else 0o644)
    scratch = out / 'shlibdeps-work'
    (scratch / 'debian').mkdir(parents=True, exist_ok=True)
    (scratch / 'debian/control').write_text('Source: obs-corridorkey\n\nPackage: obs-corridorkey\nArchitecture: any\nDescription: CorridorKey\n')
    depfile = out / f'linux-{distro}-shlibdeps.txt'
    with depfile.open('w') as stdout:
        subprocess.run(['dpkg-shlibdeps', '--ignore-missing-info', '-O', '-e' + str(module.resolve()),
                        '-e' + str(helper.resolve()), '-e' + str((helper.parent / 'corridorkey-runtime').resolve())],
                       check=True, cwd=scratch, stdout=stdout)
    with tarfile.open(out / f'corridorkey-{version}-ubuntu-{distro}-x86_64.tar.xz', 'w:xz') as archive:
        def owner(info):
            info.uid = info.gid = 0
            info.uname = info.gname = 'root'
            return info
        archive.add(stage / 'usr', arcname='usr', filter=owner)
    control = stage / 'DEBIAN'
    control.mkdir()
    dependencies = depfile.read_text().strip().removeprefix('shlibs:Depends=')
    # libobs uses SONAME 0 across this host range; OBS >=30.2 is an independent floor.
    (control / 'control').write_text(
        f'Package: obs-corridorkey\nVersion: {version}\nArchitecture: amd64\n'
        'Maintainer: Moritz Mantel <admin@moritz-mantel.de>\n'
        f'Depends: obs-studio (>= 30.2), {dependencies}\n'
        'Section: video\nPriority: optional\n'
        'Homepage: https://github.com/MinersWin/CorridorKey-for-OBS\n'
        'Description: CorridorKey video filter for OBS Studio\n'
        ' Native plugin with a private ONNX Runtime core; models and GPU providers download separately.\n', encoding='utf-8')
    subprocess.run(['dpkg-deb', '--root-owner-group', '--build', str(stage),
                    str(out / f'corridorkey-{version}-ubuntu-{distro}-amd64.deb')], check=True)
    shutil.rmtree(scratch)


def checksums(out):
    paths = sorted(p for p in out.iterdir() if p.is_file() and p.name != 'SHA256SUMS.txt')
    for path in paths:
        if path.stat().st_size >= LIMIT:
            raise ValueError(f'GitHub asset must be smaller than 2 GiB: {path}')
    (out / 'SHA256SUMS.txt').write_text(''.join(f'{digest(p)}  {p.name}\n' for p in paths), encoding='utf-8')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--stage', type=Path, default=Path('stage'))
    parser.add_argument('--out', type=Path, default=Path('release'))
    parser.add_argument('--platform', choices=['windows-x64', 'linux-x86_64'])
    parser.add_argument('--distro', choices=['24.04', '26.04'], default='24.04')
    parser.add_argument('--cache', type=Path, default=Path('.cache/runtime-downloads'))
    parser.add_argument('--checksums-only', action='store_true')
    args = parser.parse_args()
    args.out.mkdir(parents=True, exist_ok=True)
    if not args.checksums_only:
        version = json.loads((ROOT / 'buildspec.json').read_text(encoding='utf-8-sig'))['version']
        runtimes(args.stage, args.platform, args.cache)
        data = args.stage / ('corridorkey/data' if args.platform == 'windows-x64' else 'usr/share/obs/obs-plugins/corridorkey')
        probes = probe_fixtures(data)
        inventory(args.stage, args.platform, probes)
        if args.platform == 'windows-x64':
            windows(args.stage, args.out, version)
        else:
            linux(args.stage, args.out, version, args.distro)
    checksums(args.out)
