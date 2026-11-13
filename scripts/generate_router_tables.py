Import("env")

import csv
import ipaddress
import os
import re
import tempfile
from pathlib import Path


MAC_PATTERN = re.compile(r"^[0-9A-F]{12}$")
ENABLED_STATUSES = {
    "1",
    "active",
    "activo",
    "activado",
    "enabled",
    "habilitado",
    "true",
    "yes",
}


def normalize_mac(value):
    compact = re.sub(r"[:-]", "", value.strip()).upper()
    if not MAC_PATTERN.fullmatch(compact):
        raise ValueError(f"invalid MAC address: {value!r}")
    return ":".join(compact[index : index + 2] for index in range(0, 12, 2))


def cpp_string(value):
    return (
        '"'
        + value.replace("\\", "\\\\")
        .replace('"', '\\"')
        .replace("\r", "\\r")
        .replace("\n", "\\n")
        + '"'
    )


project_dir = Path(env.subst("$PROJECT_DIR"))
firmware_version = (project_dir / "VERSION").read_text(encoding="utf-8").strip()
if not re.fullmatch(r"v\d+\.\d+\.\d+", firmware_version):
    raise ValueError(f"invalid firmware version in VERSION: {firmware_version!r}")
version_header_path = project_dir / "include" / "firmware_version.h"
version_header = (
    "#pragma once\n"
    f"#define CCTV_FIRMWARE_VERSION {cpp_string(firmware_version)}\n"
)
if (
    not version_header_path.exists()
    or version_header_path.read_text(encoding="utf-8") != version_header
):
    version_header_path.parent.mkdir(parents=True, exist_ok=True)
    version_header_path.write_text(version_header, encoding="utf-8", newline="\n")

tables_dir = project_dir / "router_tables"
index_path = tables_dir / "index.csv"
generated_path = project_dir / "src" / "router_tables_generated.cpp"

with index_path.open("r", encoding="utf-8-sig", newline="") as index_file:
    index_entries = list(csv.DictReader(index_file))

if not index_entries:
    raise RuntimeError(f"No router tables listed in {index_path}")

binding_records = []
table_records = []
gateway_keys = set()

for index_entry in index_entries:
    gateway_ip = ipaddress.IPv4Address(index_entry["gateway_ip"].strip())
    gateway_mac = normalize_mac(index_entry["gateway_mac"])
    subnet = ipaddress.IPv4Network(index_entry["subnet"].strip(), strict=False)
    if gateway_ip not in subnet:
        raise ValueError(f"gateway {gateway_ip} is outside {subnet}")

    gateway_key = (str(gateway_ip), gateway_mac)
    if gateway_key in gateway_keys:
        raise ValueError(f"duplicate gateway entry: {gateway_ip} {gateway_mac}")
    gateway_keys.add(gateway_key)

    filename = index_entry["table_file"].strip()
    if Path(filename).name != filename or Path(filename).suffix.lower() != ".csv":
        raise ValueError(f"invalid table filename: {filename!r}")
    table_path = tables_dir / filename
    with table_path.open("r", encoding="utf-8-sig", newline="") as table_file:
        reader = csv.DictReader(table_file)
        if not reader.fieldnames:
            raise ValueError(f"table has no CSV header: {table_path}")
        columns = {name.strip().casefold(): name for name in reader.fieldnames}
        ip_column = columns.get("ip address")
        mac_column = columns.get("mac address")
        status_column = columns.get("status")
        description_column = columns.get("description")
        if not ip_column or not mac_column:
            raise ValueError(f"table is missing IP Address or MAC Address: {table_path}")

        table_start = len(binding_records)
        gateway_matches = 0
        seen_ips = set()
        for row in reader:
            ip = ipaddress.IPv4Address(row[ip_column].strip())
            if ip not in subnet:
                raise ValueError(f"{ip} is outside {subnet} in {table_path}")
            if str(ip) in seen_ips:
                raise ValueError(f"duplicate IP {ip} in {table_path}")
            seen_ips.add(str(ip))

            mac = normalize_mac(row[mac_column])
            if ip == gateway_ip:
                gateway_matches += 1
                if mac != gateway_mac:
                    raise ValueError(f"gateway MAC mismatch in {table_path}")

            description = (
                row.get(description_column, "").strip()
                if description_column
                else ""
            ) or f"Equipo_{ip}"
            status = (
                row.get(status_column, "").strip().casefold()
                if status_column
                else ""
            )
            enabled = not status or status in ENABLED_STATUSES
            binding_records.append(
                (str(ip), mac, description, enabled)
            )

        if gateway_matches != 1:
            raise ValueError(
                f"expected one gateway row for {gateway_ip} in {table_path}"
            )
        table_records.append(
            (
                str(gateway_ip),
                gateway_mac,
                index_entry["brand"].strip(),
                str(subnet),
                table_start,
                len(binding_records) - table_start,
            )
        )

lines = [
    '#include "router_tables_data.h"',
    "",
    "const RouterBindingRecord routerBindingRecords[] = {",
]
for ip, mac, description, enabled in binding_records:
    lines.append(
        f"    {{{cpp_string(ip)}, {cpp_string(mac)}, "
        f"{cpp_string(description)}, {'true' if enabled else 'false'}}},"
    )
lines.extend(["};", "", "const RouterTableRecord routerTableRecords[] = {"])
for gateway_ip, gateway_mac, brand, subnet, first_binding, binding_count in table_records:
    lines.append(
        f"    {{{cpp_string(gateway_ip)}, {cpp_string(gateway_mac)}, "
        f"{cpp_string(brand)}, {cpp_string(subnet)}, "
        f"{first_binding}, {binding_count}}},"
    )
lines.extend(
    [
        "};",
        "",
        "const size_t routerBindingRecordCount =",
        "    sizeof(routerBindingRecords) / sizeof(routerBindingRecords[0]);",
        "const size_t routerTableRecordCount =",
        "    sizeof(routerTableRecords) / sizeof(routerTableRecords[0]);",
        "",
    ]
)
generated_source = "\n".join(lines)

if not generated_path.exists() or generated_path.read_text(encoding="utf-8") != generated_source:
    generated_path.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.NamedTemporaryFile(
        "w",
        encoding="utf-8",
        newline="\n",
        dir=generated_path.parent,
        delete=False,
    ) as temporary_file:
        temporary_file.write(generated_source)
        temporary_path = Path(temporary_file.name)
    os.replace(temporary_path, generated_path)

print(
    "Embedded router tables: "
    f"{len(table_records)} tables, {len(binding_records)} bindings"
)
