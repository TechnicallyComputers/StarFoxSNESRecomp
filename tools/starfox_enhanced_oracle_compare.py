#!/usr/bin/env python3
"""Compare Star Fox Enhanced oracle-compatible TCP endpoints.

This is a dev harness for the local-only starfox_tcp_oracle fork. V1 attaches
to already-running endpoints so Windows command construction stays explicit.
When StarFoxSNESRecomp exposes the same semantic commands, use it as side A and
the local Star Fox Enhanced oracle as side B.
"""

from __future__ import annotations

import argparse
import socket
import sys
from pathlib import Path


class Side:
    def __init__(self, name: str, port: int) -> None:
        self.name = name
        self.port = port
        self.sock = socket.create_connection(("127.0.0.1", port), timeout=20)
        self.sock.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
        self.buf = b""
        self.hello = self._line()

    def _line(self) -> str:
        while b"\n" not in self.buf:
            chunk = self.sock.recv(65536)
            if not chunk:
                raise RuntimeError(f"{self.name}: connection closed")
            self.buf += chunk
        line, self.buf = self.buf.split(b"\n", 1)
        return line.decode(errors="replace").strip()

    def cmd(self, command: str) -> str:
        self.sock.sendall((command + "\n").encode())
        return self._line()

    def multi(self, command: str, end: str) -> list[str]:
        self.sock.sendall((command + "\n").encode())
        lines: list[str] = []
        while True:
            line = self._line()
            if line == end:
                return lines
            lines.append(line)

    def close(self) -> None:
        try:
            self.cmd("quit")
        except OSError:
            pass
        self.sock.close()


def kv(line: str) -> dict[str, str]:
    result: dict[str, str] = {}
    for word in line.split():
        if "=" in word:
            key, value = word.split("=", 1)
            result[key] = value
    return result


def host_path(path: Path) -> str:
    text = str(path.resolve()).replace("\\", "/")
    parts = text.split("/")
    if text.startswith("/") and len(parts) > 2 and len(parts[1]) == 1:
        return parts[1].upper() + ":/" + "/".join(parts[2:])
    return text


def write_snapshot(side: Side, out_dir: Path, prefix: str) -> None:
    out_dir.mkdir(parents=True, exist_ok=True)
    (out_dir / f"{prefix}_status.txt").write_text(
        side.cmd("status") + "\n", encoding="utf-8"
    )
    (out_dir / f"{prefix}_ppu.txt").write_text(
        side.cmd("ppu") + "\n", encoding="utf-8"
    )
    (out_dir / f"{prefix}_render_stats.txt").write_text(
        side.cmd("render_stats") + "\n", encoding="utf-8"
    )
    (out_dir / f"{prefix}_draw_order.txt").write_text(
        side.cmd("draw_order") + "\n", encoding="utf-8"
    )
    (out_dir / f"{prefix}_objects.txt").write_text(
        "\n".join(side.multi("objects", "objects-end")) + "\n", encoding="utf-8"
    )
    (out_dir / f"{prefix}_poses.txt").write_text(
        "\n".join(side.multi("poses", "poses-end")) + "\n", encoding="utf-8"
    )
    bmp = out_dir / f"{prefix}.bmp"
    reply = side.cmd(f"screenshot_file {host_path(bmp)}")
    if not reply.startswith("ok"):
        raise RuntimeError(f"{side.name}: screenshot failed: {reply}")


def compare_status(a_status: str, b_status: str) -> tuple[bool, str]:
    a = kv(a_status)
    b = kv(b_status)
    required = ["frame", "logic", "flow", "width", "height", "hash"]
    missing = [key for key in required if key not in a or key not in b]
    if missing:
        return False, f"missing status keys: {missing}"
    for key in required:
        if a[key] != b[key]:
            return False, f"{key}: A={a[key]} B={b[key]}"
    return True, "match"


def build_arg_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--a-port", type=int, required=True)
    parser.add_argument("--b-port", type=int)
    parser.add_argument("--steps", type=int, default=0)
    parser.add_argument("--stride", type=int, default=1)
    parser.add_argument("--out-dir", type=Path, default=Path("_oracle_compare"))
    parser.add_argument("--dump-prefix", default="oracle")
    parser.add_argument(
        "--dump-only",
        action="store_true",
        help="Capture one endpoint snapshot instead of requiring side B.",
    )
    return parser


def main(argv: list[str] | None = None) -> int:
    args = build_arg_parser().parse_args(argv)
    a = Side("A", args.a_port)
    b = None if args.dump_only else Side("B", args.b_port or 4571)
    try:
        print(f"A hello: {a.hello}")
        if b is None:
            if args.steps:
                print(f"A step: {a.cmd(f'step {args.steps}')}")
            write_snapshot(a, args.out_dir, args.dump_prefix)
            print(f"snapshot written to {args.out_dir}")
            return 0

        print(f"B hello: {b.hello}")
        for checkpoint in range(args.steps + 1):
            if checkpoint:
                a_status = a.cmd(f"step {args.stride}")
                b_status = b.cmd(f"step {args.stride}")
            else:
                a_status = a.cmd("status")
                b_status = b.cmd("status")
            ok, reason = compare_status(a_status, b_status)
            if not ok:
                print(f"first mismatch at checkpoint {checkpoint}: {reason}")
                write_snapshot(a, args.out_dir, f"A_cp{checkpoint}")
                write_snapshot(b, args.out_dir, f"B_cp{checkpoint}")
                return 1
        print(f"matched through {args.steps} checkpoints")
        return 0
    finally:
        a.close()
        if b is not None:
            b.close()


if __name__ == "__main__":
    raise SystemExit(main())
