/* SPDX-License-Identifier: GPL-3.0-only */
/* The flash parts the firmware knows, by JEDEC id (0x9F). All are 1 MiB and take the same plain SPI NOR commands
 * (0x03/0x0B read, 0x02 program, 0x20 4 KiB erase, 0x05/0x35 status, 0x06 write enable).
 *   FM-1:    0x856014  Puya P25Q80, the FM-1 as first sold
 *            0xB36014  UC25HQ80, FM-1 units sold from late 2026 (mostly AliExpress) (Clipper 1.3.1)
 *   SMK-37:  0xEB6014  the SMK-37 Pro's part, read from a unit over mask-ROM UBOOT (2026-10-09; jl-uboot-tool
 *                      "Online device: ID: 0xeb6014") (Clipper Pro)
 *            0x856014  the Puya, as on the FM-1
 * Any other id: the flash is left alone (saves stay in RAM, the update loader refuses). The one place for the app
 * (project.c, recovery.c), the USB rescue and the update loader (loader.c). */
#pragma once
#ifndef FL_JEDEC_OK
#if BOARD_SMK37
#define FL_JEDEC_OK(id) ((id) == 0xEB6014u || (id) == 0x856014u)
#else
#define FL_JEDEC_OK(id) ((id) == 0x856014u || (id) == 0xB36014u)
#endif
#endif
