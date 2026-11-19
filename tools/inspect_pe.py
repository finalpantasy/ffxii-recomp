import argparse
import hashlib
import json
import math
import struct
from collections import Counter
from datetime import datetime, timezone
from pathlib import Path

DIRECTORIES = {
    0: "export",
    1: "import",
    2: "resource",
    3: "exception",
    4: "security",
    5: "base_relocation",
    6: "debug",
    9: "tls",
    10: "load_config",
    13: "delay_import",
}


def unpack(data, fmt, offset):
    size = struct.calcsize(fmt)
    if offset < 0 or offset + size > len(data):
        raise ValueError(f"read past end of file at 0x{offset:x}")
    return struct.unpack_from(fmt, data, offset)


def c_string(data, offset, limit=4096):
    if offset < 0 or offset >= len(data):
        raise ValueError(f"string offset outside file: 0x{offset:x}")
    end = data.find(b"\0", offset, min(len(data), offset + limit))
    if end < 0:
        raise ValueError(f"unterminated string at 0x{offset:x}")
    return data[offset:end].decode("utf-8", errors="replace")


def entropy(blob):
    if not blob:
        return None
    counts = Counter(blob)
    return -sum((count / len(blob)) * math.log2(count / len(blob)) for count in counts.values())


def inspect(path):
    data = path.read_bytes()
    if not data.startswith(b"MZ"):
        raise ValueError("not a DOS/PE image")
    pe_offset = unpack(data, "<I", 0x3C)[0]
    if data[pe_offset:pe_offset + 4] != b"PE\0\0":
        raise ValueError("missing PE signature")
    coff_offset = pe_offset + 4
    machine, section_count, timestamp, _, _, optional_size, characteristics = unpack(
        data, "<HHIIIHH", coff_offset
    )
    optional_offset = coff_offset + 20
    magic = unpack(data, "<H", optional_offset)[0]
    if magic != 0x20B:
        raise ValueError(f"expected PE32+, got optional-header magic 0x{magic:x}")
    entry_rva = unpack(data, "<I", optional_offset + 16)[0]
    image_base = unpack(data, "<Q", optional_offset + 24)[0]
    subsystem = unpack(data, "<H", optional_offset + 68)[0]
    directory_count = unpack(data, "<I", optional_offset + 108)[0]
    directory_offset = optional_offset + 112
    directories = {}
    for index in range(min(directory_count, 16)):
        rva, size = unpack(data, "<II", directory_offset + index * 8)
        directories[DIRECTORIES.get(index, str(index))] = {"rva": rva, "size": size}

    sections = []
    section_offset = optional_offset + optional_size
    for index in range(section_count):
        offset = section_offset + index * 40
        name = data[offset:offset + 8].rstrip(b"\0").decode("ascii", errors="replace")
        virtual_size, virtual_address, raw_size, raw_offset = unpack(data, "<IIII", offset + 8)
        flags = unpack(data, "<I", offset + 36)[0]
        blob = data[raw_offset:raw_offset + raw_size]
        sections.append({
            "name": name,
            "virtual_address": virtual_address,
            "virtual_size": virtual_size,
            "raw_offset": raw_offset,
            "raw_size": raw_size,
            "characteristics": flags,
            "entropy": entropy(blob),
            "zero_ratio": blob.count(0) / len(blob) if blob else None,
            "int3_ratio": blob.count(0xCC) / len(blob) if blob else None,
        })

    def file_offset(rva):
        for section in sections:
            start = section["virtual_address"]
            span = max(section["virtual_size"], section["raw_size"])
            delta = rva - start
            if 0 <= delta < span:
                if delta >= section["raw_size"]:
                    raise ValueError(f"RVA 0x{rva:x} is not file-backed")
                return section["raw_offset"] + delta
        raise ValueError(f"RVA 0x{rva:x} has no section mapping")

    def import_names(thunk_rva):
        names = []
        if not thunk_rva:
            return names
        offset = file_offset(thunk_rva)
        for index in range(65536):
            value = unpack(data, "<Q", offset + index * 8)[0]
            if value == 0:
                return names
            if value & (1 << 63):
                names.append({"ordinal": value & 0xFFFF})
            else:
                name_offset = file_offset(value) + 2
                names.append({"name": c_string(data, name_offset)})
        raise ValueError(f"unterminated import thunk table at RVA 0x{thunk_rva:x}")

    imports = []
    import_dir = directories.get("import", {"rva": 0, "size": 0})
    if import_dir["rva"]:
        offset = file_offset(import_dir["rva"])
        for index in range(4096):
            original, time_date, forwarder, name_rva, first = unpack(data, "<IIIII", offset + index * 20)
            if not any((original, time_date, forwarder, name_rva, first)):
                break
            functions = import_names(original or first)
            imports.append({
                "dll": c_string(data, file_offset(name_rva)),
                "function_count": len(functions),
                "functions": functions,
            })
        else:
            raise ValueError("unterminated import descriptor table")

    delay_imports = []
    delay_dir = directories.get("delay_import", {"rva": 0, "size": 0})
    if delay_dir["rva"]:
        offset = file_offset(delay_dir["rva"])
        for index in range(4096):
            fields = unpack(data, "<IIIIIIII", offset + index * 32)
            if not any(fields):
                break
            attributes, name_rva, _, _, names_rva = fields[:5]
            if attributes & 1:
                name_rva -= image_base
                names_rva -= image_base
            functions = import_names(names_rva)
            delay_imports.append({
                "dll": c_string(data, file_offset(name_rva)),
                "function_count": len(functions),
                "functions": functions,
            })
        else:
            raise ValueError("unterminated delay-import descriptor table")

    exception_dir = directories.get("exception", {"rva": 0, "size": 0})
    reloc_dir = directories.get("base_relocation", {"rva": 0, "size": 0})
    return {
        "file_name": path.name,
        "file_size": len(data),
        "sha256": hashlib.sha256(data).hexdigest(),
        "machine": f"0x{machine:04x}",
        "pe_timestamp": timestamp,
        "pe_timestamp_utc": datetime.fromtimestamp(timestamp, timezone.utc).isoformat(),
        "image_base": f"0x{image_base:x}",
        "entry_rva": f"0x{entry_rva:x}",
        "subsystem": subsystem,
        "characteristics": f"0x{characteristics:04x}",
        "directories": directories,
        "sections": sections,
        "exception_entry_count": exception_dir["size"] // 12,
        "base_relocation_bytes": reloc_dir["size"],
        "imports": imports,
        "delay_imports": delay_imports,
        "imported_dll_count": len(imports),
        "imported_function_count": sum(item["function_count"] for item in imports),
        "delay_imported_dll_count": len(delay_imports),
        "delay_imported_function_count": sum(item["function_count"] for item in delay_imports),
    }


def main():
    parser = argparse.ArgumentParser(description="Inspect a PE32+ executable without executing it")
    parser.add_argument("path", type=Path)
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()
    result = inspect(args.path)
    text = json.dumps(result, indent=2) + "\n"
    if args.output:
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(text, encoding="utf-8")
    print(text, end="")


if __name__ == "__main__":
    main()
