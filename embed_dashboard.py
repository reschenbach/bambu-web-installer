
from pathlib import Path

# Locate the project directory
project_dir = Path(__file__).resolve().parent

# Source HTML dashboard
html_file = project_dir / "dashboard.html"

# Output header
include_dir = project_dir / "include"
include_dir.mkdir(parents=True, exist_ok=True)

header_file = include_dir / "dashboard_embedded.h"

# Read dashboard HTML
html = html_file.read_bytes()

# Convert HTML to a C++ byte array
hex_lines = [
    "  " + ", ".join(
        f"0x{byte:02x}" for byte in html[i:i + 24]
    )
    for i in range(0, len(html), 24)
]

# Write generated header
header_file.write_text(
    "#pragma once\n"
    "#include <Arduino.h>\n"
    "const char DASHBOARD_HTML[] PROGMEM = {\n"
    + ",\n".join(hex_lines)
    + ", 0x00\n"
    "};\n",
    encoding="utf-8"
)

print(f"Generated {header_file}")
print(f"Dashboard size: {len(html)} bytes")
