#!/usr/bin/env python3
"""Resolve declared maintained source revisions once for a custom image build."""
import argparse
import json
from pathlib import Path
import subprocess


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--github-output', type=Path)
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[1]
    # Read declarative repository/branch assignments, never execute arbitrary
    # source-config contents in this resolver.
    config = {}
    for line in (root / 'source-checkouts.conf').read_text().splitlines():
        if '=' in line and not line.startswith('#'):
            key, value = line.split('=', 1)
            config[key] = value
    resolved = {}
    for component in ('KERNEL', 'TME', 'INPUTPLUMBER', 'MOTION', 'DUMP',
                      'HEXAGONRPC', 'LIBSSC', 'POWERDEVIL', 'GAMESCOPE', 'SCX',
                      'HANDHELDDASH', 'FCITX5_OSK'):
        repository = config[f'{component}_SOURCE_REPOSITORY']
        branch = config[f'{component}_SOURCE_BRANCH']
        if subprocess.run(['git', 'check-ref-format', f'refs/heads/{branch}'],
                          capture_output=True).returncode:
            raise ValueError(f'{component}: invalid source branch')
        ref = config.get(f'{component}_SOURCE_REVISION', branch)
        revision = subprocess.check_output(
            ['gh', 'api', f'repos/{repository}/commits/{ref}', '--jq', '.sha'],
            text=True).strip()
        if len(revision) != 40 or any(c not in '0123456789abcdef' for c in revision):
            raise ValueError(f'{component}: invalid resolved revision')
        resolved[component.lower()] = {
            'repository': repository, 'branch': branch, 'ref': ref, 'commit': revision}
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(resolved, indent=2) + '\n')
    if args.github_output:
        with args.github_output.open('a') as output:
            for component, item in resolved.items():
                output.write(f'{component}={item["commit"]}\n')
    print(json.dumps(resolved, indent=2))


if __name__ == '__main__':
    main()
