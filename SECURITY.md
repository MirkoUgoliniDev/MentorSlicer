# Security policy

## Reporting a vulnerability

Please report security issues privately through GitHub: **Security → Report a vulnerability** on this repository. Do not open a public issue for them.

Useful details: the MentorSlicer commit or version (Help → About MentorSlicer), what an attacker needs (a crafted project or model file, access to the local network, a malicious MCP client), and the steps to reproduce.

## Scope

- The application and its local services: the Print Advisor API on `127.0.0.1` (session token in `endpoint.json`), the local web server for the embedded pages, the LAN connection to the printer.
- The MCP sidecar (`tools/print-advisor-mcp`, `mentorslicer-mcp.exe`).
- File import (3MF, STL, STEP, glTF and other model formats) and the third-party libraries listed in Help → Third-party libraries.

Issues in Creality Print code that also affect the upstream project should be reported to Creality as well.

## Supported versions

Only the latest release (see [Releases](https://github.com/MirkoUgoliniDev/MentorSlicer/releases)) and the `main` branch receive fixes; there are no backports to older releases.
