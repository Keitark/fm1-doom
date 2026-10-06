"""Read emitted PI32 stack frames; deliberately not a whole-program proof.

Inputs are the final ELF and the pinned LLVM tools. All downward SP operations
are counted, including register saves outside the entry block. This is
conservative for disjoint branch allocations. Unrecognized SP mutation fails.
Calls are reported by address to expose LTO inlining and unnamed displacements.
"""
import argparse
import hashlib
import json
import re
import subprocess
from pathlib import Path


def parse_symbols(text):
    result = {}
    for line in text.splitlines():
        match = re.fullmatch(r"([0-9a-fA-F]+)\s+([0-9a-fA-F]+)\s+[tTnN]\s+(\S+)", line)
        if match and int(match[2], 16):
            result[match[3]] = (int(match[1], 16), int(match[2], 16))
    return result


def register_bytes(text):
    text = text.strip().removeprefix("{").removesuffix("}")
    count = 0
    for part in text.split(","):
        part = part.strip()
        match = re.fullmatch(r"r(\d+)-r(\d+)", part)
        if match:
            first, last = map(int, match.groups())
            if min(first, last) < 0 or max(first, last) > 15:
                raise ValueError("unreviewed PI32 register range: " + part)
            count += abs(first - last) + 1
        elif re.fullmatch(r"r(?:[0-9]|1[0-5])|rets|reti|psr|icfg", part):
            count += 1
        else:
            raise ValueError("unreviewed PI32 pushed register: " + part)
    return count * 4


def frame_instructions(instructions, start, size):
    frame = 0
    operations, calls, indirect, tails = [], [], [], []
    for index, (address, instruction) in enumerate(instructions):
        if not start <= address < start + size:
            continue
        push = re.fullmatch(r"\[--sp\]\s*=\s*(.+)", instruction)
        change = re.fullmatch(r"sp\s*\+=\s*(-?\d+)", instruction)
        if push:
            amount = register_bytes(push[1])
            frame += amount
            operations.append({"address": hex(address), "bytes": amount, "instruction": instruction})
        elif change:
            amount = int(change[1])
            if amount < 0:
                frame -= amount
                operations.append({"address": hex(address), "bytes": -amount, "instruction": instruction})
        elif re.match(r"sp\s*[=+*/-]|\[\s*--?sp\s*\]", instruction):
            raise ValueError("unreviewed SP mutation at " + hex(address) + ": " + instruction)
        call = re.match(r"call\s+(-?\d+)(?:\s|$)", instruction)
        if call:
            if index + 1 >= len(instructions):
                raise ValueError("missing instruction after call")
            next_address = instructions[index + 1][0]
            if next_address - address not in (2, 4, 6):
                raise ValueError("unreviewed PI32 call instruction length")
            target = next_address + int(call[1])
            annotated = re.search(r"<.* : ([0-9a-fA-F]+) >", instruction)
            if annotated and int(annotated[1], 16) != target:
                raise ValueError("call target differs from LLVM annotation")
            calls.append(target)
        elif re.match(r"call\s+", instruction):
            indirect.append({"address": hex(address), "instruction": instruction})
        branch = re.match(r"goto\s+(-?\d+)(?:\s|$)", instruction)
        if branch:
            if index + 1 >= len(instructions):
                raise ValueError("missing instruction after branch")
            target = instructions[index + 1][0] + int(branch[1])
            if not start <= target < start + size:
                tails.append({"address": hex(address), "instruction": instruction})
        elif re.match(r"goto\s+", instruction):
            tails.append({"address": hex(address), "instruction": instruction})
    return {"address": hex(start), "size": size, "frame_bytes": frame,
            "stack_operations": operations, "direct_call_addresses": sorted(set(calls)),
            "indirect_calls": indirect, "tail_calls": tails}


def inspect_text(symbol_text, disassembly):
    symbols = parse_symbols(symbol_text)
    instructions = []
    for line in disassembly.splitlines():
        match = re.match(r"^\s*([0-9a-fA-F]+):\s*(.*)", line)
        if match:
            instructions.append((int(match[1], 16), match[2].strip()))
    selected = ("fm1_doom_usb_task", "command", "fm1_doom_usb_protocol_feed",
                "fm1_doom_usb_protocol_status", "fm1_doom_usb_protocol_trace",
                "fm1_doom_usb_protocol_audio", "fm1_doom_usb_protocol_volume", "fm1_doom_usb_protocol_usb_audio", "game_status", "frame_info", "frame_read",
                "fm1_usb_audio_target_status",
                "get_status", "get_game", "snprintf", "vsnprintf", "decimal", "repeat", "string",
                "audio_isr", "iis_irq_handler", "output", "scan_isr", "timer1_isr", "timer4_isr", "usb0_g_isr", "fm1_usb_rx_irq",
                "fm1_doom_sound_get_volume_hardware", "fm1_doom_usb_get_volume", "get_volume", "volume_take", "volume_release")
    by_address = {a: name for name, (a, size) in symbols.items()}
    frames = {}
    for name, (start, size) in symbols.items():
        if name in selected or any(name.startswith(n + ".") for n in selected):
            if not any(start <= a < start + size and not re.match(r"[0-9a-f]{2}\s+<", t)
                       for a, t in instructions):
                continue
            item = frame_instructions(instructions, start, size)
            item["direct_calls"] = [by_address.get(address, hex(address)) for address in item.pop("direct_call_addresses")]
            frames[name] = item
    return {"functions": frames,
            "missing_boundaries": [n for n in selected[:10] if n not in frames],
            "limitation": "Indirect callbacks, SDK recursion/tail calls, IRQ nesting and hardware context require an explicit reviewed bound; absence of a large frame alone does not prove a task fits."}


def inspect(elf, tc):
    symbols = subprocess.check_output([str(tc / "llvm-nm.exe"), "-S", "--size-sort", str(elf)], text=True)
    disassembly = subprocess.check_output([str(tc / "llvm-objdump.exe"), "-d", "-no-show-raw-insn", str(elf)], text=True)
    return {"elf_sha256": hashlib.sha256(elf.read_bytes()).hexdigest(),
            **inspect_text(symbols, disassembly)}


def usb_diagnostic_budget(report, stack_bytes):
    """Gate the LTO diagnostic closure, retaining margin for reviewed SDK calls.

    Interrupts use a separate SSP. This bound applies to the task USP only;
    runtime high-water telemetry remains required for the rest of the SDK.
    """
    frames = report["functions"]
    task_name = "fm1_doom_usb_task"
    if task_name not in frames:
        raise ValueError("USB task emitted frame is missing")
    task = frames[task_name]
    dispatch = [name for name in task["direct_calls"]
                if name in frames and (name == "command" or name.startswith("command."))]
    if len(dispatch) != 1:
        raise ValueError("USB command stack boundary is missing or ambiguous after LTO")
    dispatcher = frames[dispatch[0]]
    required = ("fm1_doom_usb_protocol_status", "fm1_doom_usb_protocol_trace",
                "fm1_doom_usb_protocol_audio", "fm1_doom_usb_protocol_volume", "fm1_doom_usb_protocol_usb_audio", "game_status", "frame_info", "frame_read")
    handlers = []
    for base in required:
        candidates = [name for name in dispatcher["direct_calls"] if name in frames
                      and (name == base or name.startswith(base + "."))]
        if len(candidates) != 1:
            raise ValueError("USB reply stack boundary is missing after LTO: " + base)
        handlers.append(candidates[0])
    format_names = ("snprintf", "vsnprintf", "decimal", "repeat", "string")
    if any(name not in frames for name in format_names):
        raise ValueError("USB formatter stack boundary is missing")
    if any(frames[name]["indirect_calls"] for name in [task_name, dispatch[0], *handlers, *format_names]):
        raise ValueError("USB diagnostics contain an unreviewed indirect call")
    if any(frames[name].get("tail_calls") for name in [task_name, dispatch[0], *handlers, *format_names]):
        raise ValueError("USB diagnostics contain an unreviewed external or indirect tail call")
    if any(set(frames[name]["direct_calls"]) & {task_name, dispatch[0], *handlers} for name in handlers):
        raise ValueError("USB diagnostic handlers contain a nested diagnostic call")
    if set(frames["snprintf"]["direct_calls"]) != {"vsnprintf"}:
        raise ValueError("USB formatter call chain changed")
    if not set(frames["vsnprintf"]["direct_calls"]).issubset({"decimal", "repeat", "string"}):
        raise ValueError("USB formatter has an unreviewed nested call")
    if any(frames[name]["direct_calls"] for name in ("decimal", "repeat", "string")):
        raise ValueError("USB formatter helper has an unreviewed nested call")
    formatter = frames["snprintf"]["frame_bytes"] + frames["vsnprintf"]["frame_bytes"] + max(
        frames[name]["frame_bytes"] for name in ("decimal", "repeat", "string"))
    audio_handler = next(name for name in handlers if name.startswith("fm1_doom_usb_protocol_usb_audio"))
    audio_helpers = [name for name in frames[audio_handler]["direct_calls"]
                     if name in frames and name.startswith("fm1_usb_audio_target_status")]
    if len(audio_helpers) != 1:
        raise ValueError("USB audio formatter stack boundary is missing")
    helper = audio_helpers[0]
    if frames[helper]["indirect_calls"] or frames[helper].get("tail_calls"):
        raise ValueError("USB audio formatter contains an unreviewed indirect or tail call")
    handler_sizes = {name: frames[name]["frame_bytes"] +
                     (frames[helper]["frame_bytes"] if name == audio_handler else 0)
                     for name in handlers}
    largest = max(handlers, key=handler_sizes.get)
    chain = task["frame_bytes"] + dispatcher["frame_bytes"] + handler_sizes[largest] + formatter
    margin = 1024
    if chain + margin > stack_bytes:
        raise ValueError(f"USB diagnostic stack {chain} plus SDK margin {margin} exceeds {stack_bytes}")
    return {"task_stack_bytes": stack_bytes, "task_frame_bytes": task["frame_bytes"],
            "diagnostic_chain_bytes": chain, "sdk_margin_bytes": margin,
            "remaining_after_chain_and_margin_bytes": stack_bytes - chain - margin,
            "chain": [task_name, dispatch[0], largest, "snprintf", "vsnprintf", "largest integer helper"],
            "handler_frames": {name: frames[name]["frame_bytes"] for name in handlers},
            "usb_audio_formatter_frame_bytes": frames[helper]["frame_bytes"],
            "scope": "emitted diagnostic USP frames; SDK callback paths and separate IRQ SSP retain live acceptance"}


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("elf", type=Path)
    parser.add_argument("--tc", type=Path, default=Path("C:/JL/pi32/bin"))
    parser.add_argument("--out", type=Path)
    args = parser.parse_args()
    report = inspect(args.elf, args.tc)
    text = json.dumps(report, indent=2)
    if args.out:
        args.out.write_text(text + "\n", encoding="utf-8")
    print(text)
