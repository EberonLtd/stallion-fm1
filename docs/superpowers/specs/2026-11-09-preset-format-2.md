# Preset format 2: 24 matrix slots per sound

Status: decided (2026-11-09), built on branch `preset-format-2`.

## Decisions

- Each sound has 24 matrix slots (cables), up from 12.
- This is Stallion's first release, so there are no Stallion presets to migrate. The user-preset store gets one
  clean layout, with no compatibility layers for Stallion's earlier tails (UP10 / UP12 / UP13, UPS9).
- People coming from Clipper or SLOOP have those firmwares' user-preset banks in the same flash sectors. The new
  format reads only its own banks, checked by magic, version, sizes and CRC. Anything else is left in flash
  untouched and not loaded: the banks read as empty. Nothing is erased until the user saves into that bank.
- Projects keep all 24 slots of both parts' sounds.
- The web editor reads and writes all 24 slots.

## Parameters

- 36 values are added: `P_M13SRC .. P_M24AMT`, after `P_MRING` and before `P_E0` (the "new common values go just
  before P_E0" rule). `P_COUNT` goes from 141 to 177.
- `mx_pid(k)` gives the slots 1..6 at `P_M1SRC`, 7..12 at `P_M7SRC` and 13..24 at `P_M13SRC`. `mx_slot_of` is the
  reverse. `MX_SLOTS` is 24 everywhere: matrix evaluation (`voice.c mx_load`), the MATRIX page, MATRIX 2 > CLEAR, the
  matrix DST names (`params.c track_desc`, now for every slot, not only 1..6) and the editor descriptors.
- The new values are device-only, like `P_FSLOPE .. P_MRING`: protocol 14 is unchanged (127 ids, `P_E0` 119).
  `P_EDHN` grows from 14 to 50, and `P_EDN` stays 127.

## User presets (flash)

The sectors stay where they are. Each of the two objects (`OBJ_UPRESET0/1`, an A/B sector pair each: FM-1
0xDC000..0xDFFFF, SMK-37 0xA5000..0xA8FFF) holds one bank of 16 sounds as its storage.c payload. No side record and
no tail.

### Bank (3,820 B of the 3,840 B payload)

| Bytes | Field |
|---|---|
| 0..3 | magic "STP2" (0x32505453, little-endian) |
| 4 | version 2 |
| 5 | slot count, 16 |
| 6..7 | record size, 238 |
| 8.. | 16 records |
| last 4 | CRC-32 (zlib) of every byte before it |

### Record (238 B)

| Bytes | Field |
|---|---|
| 0 | engine (an empty slot is all zero: its name empty) |
| 1 | nc: how many common values follow (today `P_E0`, 169) |
| 2 | ne: how many engine values follow them (8) |
| 3 | flags (bit 0: `ms` holds the MOD SEQ) |
| 4..15 | name, 1..12 printable ASCII, 0-padded |
| 16..47 | MOD SEQ: 64 steps of 4 bits (step + 8), two steps per byte |
| 48..237 | 190 value bytes: nc common values (`P_LEVEL ..`, in order), then ne engine values (`P_E0 .. P_E7`). Each is value - its minimum (`TP[i].min`, or the engine's `edit[k].min`) |

- A host test checks that every per-sound range fits a byte. Today 177 of the 190 value bytes are used, so 13 are
  spare.
- A later version adds values just before `P_E0` as before. It writes a larger nc, and up to 13 more values fit with
  no change of record size.
- A record with a smaller nc loads the missing common values at their defaults. A larger nc drops the extra values.
  Values are mapped by count, as SLOOP's records are.
- All 24 slots are kept exactly: the source and destination as full bytes (6-bit destinations included) and the
  amount as amount + 64.

### Foreign data rule

A bank is ours only if all of these hold:

- the storage.c header is valid (its own magic and CRC);
- the payload is exactly 3,820 B;
- the magic, version, slot count and record size match;
- the CRC matches.

Anything else (an empty or erased sector, a Clipper or SLOOP "UPB1" bank, a Clipper 0.4 bank, a damaged copy) reads
as 16 empty slots.

- Loading never writes flash. The foreign copy and its side record and tails stay as they are.
- A save writes our bank to the other copy of the A/B pair, as every storage.c save does. The foreign copy stays in
  the older sector until a later save reuses that sector.
- The non-synth build (`CLIPPER_SYNTH=0`, SLOOP's groovebox host tests) keeps SLOOP's UPB1 record unchanged.

### RAM

The two banks are mirrored in the pool (2 x 3,820 B), so browsing never reads flash. That replaces the 1.0 mirror:
banks 6,160 + tails 1,520 + the 0.4 import buffer 3,720 in the pool, and the side records 336 B in RAM.

## Projects

The project format (FUN5, shared with SLOOP and Clipper) does not change. Slots 1..12 stay where Clipper keeps them.
Slots 13..24 of parts 1 and 2 ride in a new block at the end of the project object's side record (storage.c
`st_side`, which SLOOP and Clipper never read). The side record is 72 B (Clipper's block) + 48 B (Stallion's block)
+ 72 B (this block) = 192 B of 192: the side record is full.

`proj_mx_t` (72 B):

| Field | Size |
|---|---|
| magic "PX24" | 4 B |
| project sum | 4 B (binds the block to its project, as `proj_xs_t`) |
| 2 parts x 12 slots x 19 bits, packed little-endian: source 6 bits, destination 6 bits, amount + 64 7 bits | 57 B |
| slot count, 12 | 1 B |
| reserved | 2 B |
| hash of every byte before | 4 B |

- The block is kept per project in RAM, in the pool beside `proj_x10s` and `proj_xss` (7 x 72 B). It is written
  with every project save and the autosave, whatever the steps hold, so a project with drum hits keeps it too.
- A project without the block loads slots 13..24 off. That covers Clipper's, SLOOP's and a damaged block.

## Editor protocol

- No new command. Protocol 15 ids 141..176 are slots 13..24 (slot k: 141 + 3 (k - 13) + field, where field is 0
  SRC, 1 DST, 2 AMT). `P_ALL` is 177.
- GET, SET, DESC scope 2, DUMP / TRACK_DUMP / UP_GET with flag bit 0, and UP_PUT's trailing values carry them as
  they carry ids 127..140.
- DESC of a slot's DST gives the engine's names for E1..E8, as slots 1..12 do.
- The web editor's MATRIX pages (`MX_PAGES`, `mxId`) cover slots 13..24 on a device whose `P_ALL` reaches 177.
  `mxSlots(info)` gives the slot count from INFO: 24, 12 or 6. The patch bay UI is not touched in this branch: it
  can take the count from `mxSlots`.
- UP_GET's 16 pattern pairs are 0 on the synth. A synth preset holds the MOD SEQ, not a pattern; before, those bytes
  were the MOD SEQ's nibbles cut to 7 bits.
- **Backups.** Objects 6 and 7 are the banks above (3,820 B each, length 0 when empty). Objects 12 and 13 (the old
  side records and tails) are gone: they are no longer listed, and a restore of them answers rc 1 (no such
  object), which the editor passes over. A restore of a bank that is not ours (a Clipper backup's UPB1 bank) answers
  rc 2. The editor then passes over that bank and restores the rest of the file, so a Clipper backup still brings
  its projects and settings. Object 14 (projects' side blocks) is 5 x 192 B: Clipper's block, Stallion's, then
  `proj_mx_t`. The older sizes (5 x 120, 5 x 108, 5 x 72) are still accepted, with slots 13..24 off.

## Device MATRIX page

- SLOT runs 1..24. The page shows the six slots of the selected one's group (1..6, 7..12, 13..18, 19..24).
- Slot numbers are drawn as two digits from 10 on.
- MATRIX 2 > CLEAR turns all 24 off.

## Cost

| | |
|---|---|
| RAM | `track_t.p` + 72 B per track (4 tracks: 288 B). The sound snapshots `syn_a/b/back`, `hist_cur/orig/cmp`: 6 x 72 B. Old side records - 336 B |
| Pool | User presets 7,640 B (was 11,400 B). Undo ring + 1,152 B. `proj_mxs` + 504 B. `bk14` + 360 B |
| Flash | Measured below |
| CPU | `mx_load` scans 24 slots instead of 12 once per block per part, a few dozen cycles. A sound's cost grows only with the slots it uses |

## Tests

These are written to fail first:

- user preset round trip of 24 slots, including 6-bit destinations;
- a bank with Clipper-format and SLOOP-format records loads as empty, and loading leaves the sector byte for byte
  as it was;
- power-cycle persistence through the storage sim;
- projects round trip with slots 13..24 and drum hits;
- editor backup and restore of all slots (objects 6/7 and 14);
- matrix evaluation with 24 live slots;
- the editor reads and writes slots 13..24 (`test_editor_p15` through `editor_bridge`, and the web test).

## Measured (2026-11-09, both device builds)

| | Before (main 19a843e) | After |
|---|---|---|
| FM-1 image | 532,892 B | 531,676 B (-1,216) |
| SMK-37 image (XIP slot 581,564 B) | 567,544 B (14,020 free) | 565,384 B (-2,160; 16,180 free) |
| FM-1 RAM .data+.bss (of 98,304) | 79,364 B | 80,312 B (+948) |
| SMK-37 RAM .data+.bss | 84,884 B | 85,864 B (+980) |
| Pool (of 344,064; both) | 331,312 B | 329,568 B (-1,744; 14,496 spare) |
| User-preset sector payload | 3,840 of 3,840 B (bank + tails) | 3,820 of 3,840 B (no side record, no tail) |
| Project side record | 120 of 192 B | 192 of 192 B |

Code shrinks because the 1.x tails, side records and the Clipper 0.4 import are gone.
