from pathlib import Path
Import('env')
root = Path(env['PROJECT_DIR'])
html = (root / 'dashboard.html').read_bytes()
header = root / 'include' / 'dashboard_embedded.h'
header.write_text('#pragma once\n#include <Arduino.h>\nconst char DASHBOARD_HTML[] PROGMEM = {\n' + ',\n'.join('  ' + ', '.join('0x%02x' % b for b in html[i:i+24]) for i in range(0,len(html),24)) + ', 0x00\n};\n')
