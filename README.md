# EditInterface

A MUI 3.8 GUI application for editing [Roadshow](http://roadshow.apc-tcp.de/index-en.php) network interface configuration files on AmigaOS 3.x.

![AmigaOS](https://img.shields.io/badge/AmigaOS-3.x-blue) ![MUI](https://img.shields.io/badge/MUI-3.8-green) ![License](https://img.shields.io/badge/License-Public%20Domain-brightgreen)

## Overview

EditInterface provides a user-friendly graphical interface to configure Roadshow TCP/IP stack network interfaces, replacing the need to manually edit text configuration files. It handles three configuration files from a single window:

- **Interface settings** — `DEVS:NetInterfaces/<interface>` (device, IP, configure mode)
- **Default gateway** — `DEVS:Internet/routes`
- **DNS servers & domain** — `DEVS:Internet/name_resolution`

## Features

- Tabbed interface with **General** and **Advanced** settings
- Supports all Roadshow interface parameters: device, unit, address, netmask, configure mode (Static IP / DHCP / Auto / Fast Auto), debug, IP and write request buffers, traffic capture filter, and init delay
- Gateway, up to 3 DNS servers, and default domain name
- Input validation (numeric-only fields, IP address format filtering)
- Bubble help on all fields
- Accepts both interface name or full file path as argument
- Menu with About, About MUI, and Quit

## Screenshots

<img width="286" height="357" alt="editinterface" src="https://github.com/user-attachments/assets/0ad917a0-8d99-4d99-a47d-76ba5da3c291" />

## Requirements

- AmigaOS 3.x (68k)
- MUI 3.8+
- [Roadshow TCP/IP stack](http://roadshow.apc-tcp.de/index-en.php)
- [OpenURL](http://aminet.net/package/comm/www/OpenURL) (optional, for the GitHub link in About)

## Usage

```
EditInterface <interface_name>
```

or with a full path:

```
EditInterface <path/to/interface_file>
```

### Examples

```
EditInterface V4Net
EditInterface DEVS:NetInterfaces/V4Net
EditInterface Work:MyConfigs/Ethernet
```

## Building

### SAS/C 6.x

```
smake
```

### VBCC

```
make -f Makefile.vbcc
```

## Configuration Files

EditInterface reads and writes the following Roadshow configuration files:

| File | Description |
|------|-------------|
| `DEVS:NetInterfaces/<name>` | Interface device and IP configuration |
| `DEVS:Internet/routes` | Default gateway route |
| `DEVS:Internet/name_resolution` | DNS servers and domain name |

## Author

**Renaud Schweingruber**

- Email: renaud.schweingruber@protonmail.com
- GitHub: [TuKo1982](https://github.com/TuKo1982)

## License

This project is released into the Public Domain.
