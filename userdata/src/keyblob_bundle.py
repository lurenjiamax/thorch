#!/usr/bin/env python3
"""Seal/import Thor KeyMint blobs without displaying secret material."""
from __future__ import annotations

import argparse
import json
import os
from pathlib import Path
import resource

from cryptography.hazmat.primitives import hashes, serialization
from cryptography.hazmat.primitives.asymmetric import padding, rsa


def private_directory(path: Path) -> None:
    if path.is_symlink():
        raise ValueError("Refusing symlink directory")
    path.mkdir(mode=0o700, parents=True, exist_ok=True)
    os.chmod(path, 0o700)


def write_new(path: Path, data: bytes, mode: int = 0o600) -> None:
    fd = os.open(path, os.O_WRONLY | os.O_CREAT | os.O_EXCL | os.O_NOFOLLOW, mode)
    with os.fdopen(fd, "wb") as stream:
        stream.write(data)
        stream.flush()
        os.fsync(stream.fileno())


def init_recipient(directory: Path) -> None:
    private_directory(directory)
    private_path = directory / "recipient-private.pem"
    public_path = directory / "recipient-public.pem"
    if private_path.exists() or public_path.exists():
        raise ValueError("Recipient already exists; preserve it, do not overwrite")
    key = rsa.generate_private_key(public_exponent=65537, key_size=4096)
    write_new(private_path, key.private_bytes(
        serialization.Encoding.PEM, serialization.PrivateFormat.PKCS8,
        serialization.NoEncryption()))
    write_new(public_path, key.public_key().public_bytes(
        serialization.Encoding.PEM, serialization.PublicFormat.SubjectPublicKeyInfo), 0o644)
    print(f"Recipient prepared: {directory} (private key not displayed)")


def import_blobs(recipient: Path, encrypted: Path, output: Path) -> None:
    fd = os.open(recipient, os.O_RDONLY | os.O_NOFOLLOW)
    with os.fdopen(fd, "rb") as stream:
        key = serialization.load_pem_private_key(stream.read(), password=None)
    if not isinstance(key, rsa.RSAPrivateKey) or key.key_size < 4096:
        raise ValueError("Expected RSA4096 recipient")
    private_directory(output)
    paths = [encrypted / f"{name}.rsa-oaep" for name in ("metadata", "user0-de", "user0-ce")]
    decoded: list[tuple[str, bytes]] = []
    # Validate every ciphertext before creating any plaintext blob files.
    for path in paths:
        fd = os.open(path, os.O_RDONLY | os.O_NOFOLLOW)
        with os.fdopen(fd, "rb") as stream:
            ciphertext = stream.read(key.key_size // 8 + 1)
        if len(ciphertext) != key.key_size // 8:
            raise ValueError("Invalid RSA ciphertext length")
        blob = key.decrypt(ciphertext, padding.OAEP(
            mgf=padding.MGF1(hashes.SHA256()), algorithm=hashes.SHA256(), label=None))
        if not blob or len(blob) > 446:
            raise ValueError("Unexpected storage blob length")
        name = path.name.removesuffix(".rsa-oaep")
        if (output / f"{name}.keyblob").exists():
            raise ValueError("Refusing to overwrite a stored keyblob")
        decoded.append((name, blob))
    for name, blob in decoded:
        write_new(output / f"{name}.keyblob", blob)
    manifest = {
        "version": 1,
        "kind": "device-bound-keymint-storage-keyblobs",
        "keys": [name for name, _ in decoded],
        "requires_original_device_keymint": True,
        "requires_fresh_ephemeral_conversion_each_boot": True,
        "contains_saved_ephemeral_keys": False,
    }
    write_new(output / "manifest.json", (json.dumps(manifest, indent=2) + "\n").encode())
    print(f"Imported {len(decoded)} device-bound blobs into {output}; secret bytes not displayed")


def main() -> None:
    resource.setrlimit(resource.RLIMIT_CORE, (0, 0))
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest="command", required=True)
    init = commands.add_parser("init-recipient")
    init.add_argument("directory", type=Path)
    unpack = commands.add_parser("import")
    unpack.add_argument("--recipient-private", type=Path, required=True)
    unpack.add_argument("--encrypted-directory", type=Path, required=True)
    unpack.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    if args.command == "init-recipient":
        init_recipient(args.directory)
    else:
        import_blobs(args.recipient_private, args.encrypted_directory, args.output)


if __name__ == "__main__":
    main()
