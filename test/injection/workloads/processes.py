"""Read process evidence without starting another Nginx process."""

import json
from pathlib import Path

processes = []
for process in Path("/proc").glob("[0-9]*"):
    try:
        command = (process / "cmdline").read_bytes().replace(b"\0",
                                                             b" ").decode()
        if (process / "comm").read_text().strip() != "nginx":
            continue
        maps = (process / "maps").read_text()
        paths = sorted({
            line.split()[-1]
            for line in maps.splitlines()
            if "ngx_http_datadog_module.so" in line
        })
        processes.append(
            dict(pid=int(process.name),
                 command=command,
                 modules=paths,
                 maps=maps))
    except (FileNotFoundError, ProcessLookupError):
        continue
print(json.dumps(processes))
