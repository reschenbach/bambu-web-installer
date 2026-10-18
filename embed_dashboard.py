
from pathlib import Path
Import("env")

# Get the project directory from PlatformIO
project_dir = Path(env.subst("$PROJECT_DIR"))

# Input dashboard HTML
html_file = project_dir / "dashboard.html"

# Output header directory
include_dir = project_dir / "include"
include_dir.mkdir(parents=True, exist_ok=True)

header_file = include_dir / "dashboard_embedded.h"

# Read the HTML file
if not html_file.exists():
    raise FileNotFoundError(
        f"Dashboard HTML not found: {html_file}"
    )

html = html_file.read_bytes()

# Convert HTML to a C++ byte array
hex_lines = [
    "  " + ", ".join(
        f"0x{byte:02x}" for byte in html[i:i + 24]
    )
    for i in range(0, len(html), 24)
]

# Generate header
header_file.write_text(
    "#pragma once\n"
    "#include <Arduino.h>\n"
    "const char DASHBOARD_HTML[] PROGMEM = {\n"
    + ",\n".join(hex_lines)
    + ", 0x00\n"
    "};\n",
    encoding="utf-8"
)

print(f"Generated: {header_file}")
print(f"Dashboard size: {len(html)} bytes")
