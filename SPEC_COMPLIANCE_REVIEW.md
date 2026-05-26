# NVMe-cli — NVM Express Base Specification Revision 2.3 Compliance Review

**Spec audited:** `NVM-Express-Base-Specification-Revision-2.3-2025.08.01-Ratified.pdf`
(784 pages, ratified 2025-07-31; incorporates rev 2.2 + TP4202, TP4153, TP4163,
TP4199, TP8028, ECN124, ECN126, ECN127, ECN128, ECN129, ECN130).

**Codebase audited (commit `07cf17ec9`):** `nvme-cli` (current working tree), incl.
in-tree `libnvme/`, `nvme-print*.c`, `plugins/` (sampled).

**Date of audit:** 2026-05-26 (expanded coverage pass added the same day).

**Issue types in scope (per requester):** wrong values/opcodes/bits, struct
layout / size bugs, and decoder/printer bugs in `nvme-print-*`. New 2.3
features that are simply *not yet implemented* are normally not flagged as
"bugs" — but where an explicit opcode/CNS/LID enum entry exists with no
backing struct/decoder, that asymmetry **is** flagged because callers can
reach a path the code can't handle.

> **Scope caveat — this is still not 100% exhaustive.** The spec is 784
> pages and the source tree is ~155 k LoC. After the expansion pass this
> audit covers: Identify (CNS 0h–20h base-spec entries), all log pages
> 00h–25h + 70h–73h + 7Fh–81h, opcode tables, status codes (all SCTs),
> Get/Set Features, Sanitize/Format/FW-Commit command CDWs, Persistent
> Event Log header and per-event-type structs, FDP, ZNS index-level
> sanity, NVMe-Fabrics Connect/Discovery/Status, and the Cross-Controller
> Reset / Live Migration / Track / Discovery-Info command families.
> Areas still NOT exhaustively covered: companion specs (NVM CS, ZNS CS,
> KV CS, Computational Programs CS), Fabric Zoning command payload
> structures (Receive/Lookup/Send), NVMe-MI command set internals,
> Discovery service workflow logic, vendor plugins (`plugins/wdc`,
> `plugins/ocp`, …), the full `Documentation/` man-page tree. Line/figure
> references are pdftotext line numbers; the canonical reference is the
> figure number in the PDF.

---

## Summary (post expansion)

| Severity | Count | One-line description                                          |
|----------|-------|---------------------------------------------------------------|
| Critical | 10    | Wrong/missing required fields & structs; status-code value collisions; missing opcode; missing event types |
| High     | 13    | Missing log-page fields; missing/unmapped spec-defined status codes; missing AEI Notice codes; CLI feature-gap; missing log-page decoders |
| Medium   | 4     | Optional command-specific status codes; AEI Immediate/One-Shot sub-values; CNS 1Ah no struct; LID 1Ch no decoder |
| Low      | 3     | Inconsistent abbreviations; Telemetry log field-name confusion; sanitize ETODMM spelling |
| Info     | 6     | Observations and scope caveats; not bugs                       |

> The original audit produced 4/7/3/1/4. The expansion pass added 6
> Critical, 6 High, 1 Medium, 2 Low, and 2 Info findings, and verified
> ~25 additional structures/log pages/commands as compliant. Two items
> (AFI bit-decoding, SMART rename) were promoted from Medium to High
> after re-evaluating user-visibility impact.

---

## CRITICAL

### C1. `struct nvme_id_ctrl` is missing CIU and CIRN (bytes 135–143)

- **Where:** `libnvme/src/nvme/nvme-types.h` line ~1537 (`__u8 rsvd135[118];`).
- **Spec:** Figure 328 "Identify – Identify Controller Data Structure, I/O
  Command Set Independent", bytes 135 and 143:136 (spec.txt lines 20530–20566).
- **What spec says:**
  - byte 135 — Controller Instance Uniquifier (CIU), 1 byte, Optional for I/O,
    Admin, *and* Discovery controllers.
  - bytes 143:136 — Controller Instance Random Number (CIRN), 8 bytes, same
    O/M profile.
  - bytes 239:144 — Reserved; bytes 252:240 — Reserved for NVMe Management
    Interface.
- **What the code does:** lumps bytes 135–252 into one `rsvd135[118]` block.
- **Impact:** CIU and CIRN are the mechanism the **Cross-Controller Reset**
  feature (Admin Opcode 38h / Log Page 1Eh) uses to detect stale controller
  identifiers (refer to spec §8.3.3, §9.6.2.2). Hosts that read this from a
  2.3-conformant controller via nvme-cli get garbage instead of the field
  contents.
- **Fix sketch:**

  ```c
  __u8     crcap;
  __u8     ciu;        /* byte 135 */
  __u8     cirn[8];    /* bytes 143:136 */
  __u8     rsvd144[96];   /* 239:144 */
  __u8     rsvd240[13];   /* 252:240 reserved for NVMe-MI */
  __u8     nvmsr;      /* byte 253 (unchanged) */
  ```

  Verified offsets and total size (4096 bytes) before/after the change.

<ref_snippet file="/home/dennis/src/nvme-cli/libnvme/src/nvme/nvme-types.h" lines="1509-1638" />

---

### C2. Status-code enum: duplicate value 0x12 (`FW_NEEDS_MAX_TIME` vs `EXCEEDS_MAX_NS_SANITIZE`)

- **Where:** `libnvme/src/nvme/nvme-types.h` lines 8542–8543.
- **What the code declares:**

  ```c
  NVME_SC_FW_NEEDS_MAX_TIME       = 0x12,
  NVME_SC_EXCEEDS_MAX_NS_SANITIZE = 0x12,   /* WRONG VALUE */
  ```

- **Spec:** Figure 103 "Status Code – Command Specific Status Values":
  - `0x12` is **Firmware Activation Requires Maximum Time Violation**
    (applies to Firmware Commit).
  - **`0x3C`** is **Request Exceeds Maximum Namespace Sanitize Operations
    In Progress** (applies to Sanitize Namespace).
- **Why this matters:** C *allows* enum aliases with the same value, so this
  compiles silently. But any code that switches on `NVME_SC_EXCEEDS_MAX_NS_SANITIZE`
  is comparing against the wrong status value — it will react to FW-commit
  errors and never to the Sanitize-Namespace error it's named for.
- **Fix:** change line 8543 to `= 0x3c,` and verify call sites.

<ref_snippet file="/home/dennis/src/nvme-cli/libnvme/src/nvme/nvme-types.h" lines="8541-8548" />

---

### C3. Status-code enum: duplicate value 0x15 (`NS_INSUFFICIENT_CAP` vs `FEAT_IOCS_COMBINATION_REJECTED`)

- **Where:** `libnvme/src/nvme/nvme-types.h` lines 8546–8547.
- **What the code declares:**

  ```c
  NVME_SC_NS_INSUFFICIENT_CAP            = 0x15,
  NVME_SC_FEAT_IOCS_COMBINATION_REJECTED = 0x15,   /* WRONG VALUE */
  ```

- **Spec:** Figure 103:
  - `0x15` is **Namespace Insufficient Capacity** (Namespace Management).
  - **`0x2B`** is **I/O Command Set Combination Rejected** (Set Features).
- **Note:** the *correctly-numbered* variant already exists separately as
  `NVME_SC_IOCS_COMBINATION_REJECTED = 0x2b` at line 8572. The buggy alias
  at line 8547 is what's wrong.
- **Fix:** either change line 8547 to `= 0x2b,` (making it a name-only alias
  of the existing `NVME_SC_IOCS_COMBINATION_REJECTED`) or delete it and
  redirect callers.

<ref_snippet file="/home/dennis/src/nvme-cli/libnvme/src/nvme/nvme-types.h" lines="8546-8572" />

---

### C4. Persistent Event Log: Event Type 0Fh (Configurable Device Personality Change) is missing

- **Where:** `enum nvme_persistent_event_types` at
  `libnvme/src/nvme/nvme-types.h` lines 4581–4598.
- **Spec:** §5.2.12.1.14.2.15 "Configurable Device Personality Change Event
  (Event Type 0Fh)" (spec.txt line 16115), Figure 253.
- **What the code does:** jumps from `NVME_PEL_SANITIZE_MEDIA_VERIF_EVENT = 0x0e`
  directly to `NVME_PEL_VENDOR_SPECIFIC_EVENT = 0xde`, skipping `0x0f`.
- **Impact:** Persistent Event Log dumps from a controller that records this
  event will display it as an unknown / unhandled event type, and no struct
  exists to decode its payload (Figure 253: Personality Status, Personality
  Identifier fields).
- **Fix:** add `NVME_PEL_CDP_CHANGE_EVENT = 0x0f,` to the enum and a
  corresponding `struct nvme_cdp_change_event` matching Figure 253:

  ```c
  struct nvme_cdp_change_event {
      __u8  ps;          /* Personality Status: bit 0 CDPCE, bit 1 CDPRFS, 7:2 rsvd */
      __u8  perid;       /* Personality Identifier */
      __u8  rsvd2[10];
      __u8  ped[];       /* Personality Event Data */
  } __attribute__((packed));
  ```

<ref_snippet file="/home/dennis/src/nvme-cli/libnvme/src/nvme/nvme-types.h" lines="4581-4598" />

---

### C5. Cross-Controller Reset admin opcode (0x38) is missing from the enum

- **Where:** `enum nvme_admin_opcode` in `libnvme/src/nvme/nvme-types.h`
  jumps from `nvme_admin_manage_export_port = 0x35` to
  `nvme_admin_send_disc_log_page = 0x39`. The value `0x38` is never
  assigned in this enum (the symbol `NVME_SC_NOT_ENOUGH_RESOURCES = 0x38`
  at line 8590 is a *status code*, in a different enum).
- **Spec:** Figure 142 (Opcodes for Admin Commands) lists
  `0x38 = Cross-Controller Reset`, defined in §5.4.3. This is a headline
  new-in-2.3 feature.
- **Impact:** No symbolic name; no `nvme_init_cross_ctrl_reset` wrapper;
  no CLI subcommand; nothing in code that lets a user issue or interpret
  the command. Combined with C1 (missing CIU/CIRN in Identify) and H3
  (missing status codes 0x3F/0x40/0x41), the entire Cross-Controller
  Reset feature is end-to-end undecoded.
- **Fix:** add `nvme_admin_cross_ctrl_reset = 0x38,` and (for any future
  CLI work) wire up an `nvme_init_cross_ctrl_reset()` analogous to the
  existing `nvme_init_lm_track_send()` style helpers. Spec field layout
  for CDW10 (ICID at 15:0, CIU at 23:16) and CDW12/13 (CIRNL/CIRNU) is
  in Figure 522.

<ref_snippet file="/home/dennis/src/nvme-cli/libnvme/src/nvme/nvme-types.h" lines="8845-8855" />

---

### C6. Identify CNS 1Dh (Get Underlying Namespace List) — struct + decoder missing

- **Where:** Nothing matching `underlying_ns`, `underly_ns`, or `id_underly`
  exists in `libnvme/src/nvme/nvme-types.h`. The CNS enum value
  `NVME_IDENTIFY_CNS_UNDERLYING_NS_LIST = 0x1D` is declared (line 8945
  area) but there is no backing data structure or decoder.
- **Spec:** §5.2.13.4.1, Figures 349 + 350. The response payload is a
  16-byte header (`GENCTR` u64, `NUMENT` u64) followed by 320-byte
  entries containing the underlying-subsystem NQN (256 bytes), NSID
  (4 bytes), CNTLID (2 bytes), reserved (58 bytes).
- **Impact:** Hosts that issue `nvme id-ctrl --cns=0x1d` (or equivalent)
  receive raw bytes with no decoded view.
- **Fix sketch:**

  ```c
  struct nvme_underlying_ns_entry {
      char    unsnqn[NVME_NQN_LENGTH];  /* bytes 255:0   */
      __le32  nsid;                     /* bytes 259:256 */
      __le16  cntlid;                   /* bytes 261:260 */
      __u8    rsvd262[58];              /* bytes 319:262 */
  };
  struct nvme_id_underlying_ns_list {
      __le64                          genctr;     /* 7:0   */
      __le64                          nument;     /* 15:8  */
      struct nvme_underlying_ns_entry entries[];  /* 16+   */
  };
  ```

  Plus corresponding `stdout_id_underlying_ns_list()` / json variant.

---

### C7. Identify CNS 1Eh (Get Ports List) — struct + decoder missing

- **Where:** Nothing matching `id_ports`, `ports_list`, `ports_entry`,
  or `id_port` defined as a CNS-1Eh response. CNS enum value
  `NVME_IDENTIFY_CNS_PORTS_LIST = 0x1E` is declared without backing
  struct.
- **Spec:** §5.2.13.4.2, Figures 351 + 352. Header `GENCTR` (u64) +
  `NUMENT` (u64); per-entry 576 bytes: TRADDR (256), TSAS (256), PIDUP
  (2), TRTYPE (1), ADRFAM (1), TREQ (1), reserved (59).
- **Impact:** Same shape as C6 — undecoded raw bytes for `--cns=0x1e`.
- **Fix sketch:** mirror C6 with a 576-byte entry struct.

---

### C8. Identify CNS 20h (Supported Controller State Formats) — struct + decoder missing

- **Where:** No matching struct in libnvme. CNS enum value
  `NVME_IDENTIFY_CNS_SUPPORTED_CTRL_STATE_FORMATS = 0x20` exists in
  the doc comments / enum but has no payload definition.
- **Spec:** §5.2.13.2.21, Figure 345. Layout is unusual: byte 0 = NV
  (number-of-versions), byte 1 = NUUID, then a variable-length array
  of 2-byte controller-state-version values (NV+1 entries), then a
  16-byte UUID array (NUUID entries). Not a fixed 4096-byte payload.
- **Impact:** Hosts cannot enumerate which controller-state-version /
  UUID combinations the controller supports for the Live Migration
  Get/Set Controller State workflows — required for Migration Send/Recv
  to know which state format to request.

---

### C9. Log Page 1Dh (Device Personalities) — struct + decoder missing

- **Where:** Nothing matching `device_personality`, `device_personalities`,
  `cdp_*log` in libnvme. The LID enum value (`NVME_LOG_LID_DEVICE_PERSONALITIES
  = 0x1D` area) is declared with no backing struct.
- **Spec:** §5.2.12.1.28, Figures 284 + 285. 6-byte header (NUMP,
  CDPLPV, DPLPHL, CDPLPS) followed by variable-length 5-byte
  Personality Properties descriptors (PPS, PERID, MRSTT, AUS, rsvd).
- **Impact:** Required log page for any controller that implements the
  Configurable Device Personality feature (Set/Get Feature 22h);
  without it, `nvme get-log --log-id=0x1d` returns raw bytes.

---

### C10. Log Page 1Eh (Cross-Controller Reset) and 1Fh (Lost Host Communication) — structs + decoders missing

- **Where:** Nothing matching `cross_ctrl_reset_log`, `cross_controller`,
  `lost_host_comm` in libnvme.
- **Spec:**
  - §5.2.12.3.1 / Figures 304 + 305: 2-byte `NE` header + 6 reserved,
    followed by 8-byte entries (ICID u16, CIU u8, rsvd u8, ACID u16,
    CCRS u8, CCRF u8). Up to 511 entries.
  - §5.2.12.3.2 / Figures 306 + 307: identical 8-byte header, 8-byte
    entries (CNTLID u16, LC u8, CIU u8, rsvd u32). Up to 511 entries.
- **Impact:** Together with C1, C5, H3 (status codes), this is another
  area where the entire Cross-Controller Reset feature lacks any
  decode support in nvme-cli.
- **Fix sketch:** mirror the existing `nvme_reachability_groups_log` /
  `nvme_reachability_group_desc` pattern.

---

## HIGH

### H1. `struct nvme_sanitize_log_page` is missing MNSOIP and STNSID

- **Where:** `libnvme/src/nvme/nvme-types.h` lines 5594–5607.
- **Spec:** Figure 302 "Sanitize Status Log Page" (spec.txt lines 18410–18700):
  - bytes 43:40 — **Maximum Namespace Sanitize Operations In Progress (MNSOIP)**,
    4 bytes, indicates the concurrency limit for Sanitize Namespace operations.
    `FFFF_FFFFh` if the target is a namespace.
  - bytes 47:44 — **Sanitization Target NSID (STNSID)**, 4 bytes, the NSID the
    log applies to when target is a namespace (else `0`).
  - bytes 511:48 — Reserved.
- **What the code does:** struct ends with `__u8 ssi; __u8 rsvd37[475];` —
  covers bytes 36 (SSI) then bytes 37–511 as one reserved block, hiding
  MNSOIP and STNSID inside `rsvd37[]`.
- **Decoder impact:** `nvme-print-stdout.c::stdout_sanitize_log` (line 4837)
  and `nvme-print-json.c::json_sanitize_log` (line 1464) print everything up
  to SSI and then stop — users running `nvme sanitize-log` on a controller
  that supports Sanitize Namespace get no visibility into the new fields.
- **Fix sketch:**

  ```c
  __u8     ssi;          /* byte 36 */
  __u8     rsvd37[3];    /* bytes 39:37 */
  __le32   mnsoip;       /* bytes 43:40 */
  __le32   stnsid;       /* bytes 47:44 */
  __u8     rsvd48[464];  /* bytes 511:48 */
  ```

  Plus add prints in both stdout/json decoders.

<ref_snippet file="/home/dennis/src/nvme-cli/libnvme/src/nvme/nvme-types.h" lines="5594-5607" />
<ref_snippet file="/home/dennis/src/nvme-cli/nvme-print-stdout.c" lines="4837-4876" />
<ref_snippet file="/home/dennis/src/nvme-cli/nvme-print-json.c" lines="1464-1514" />

---

### H2. Missing Generic status codes 0x2B and 0x2C (Sanitize Namespace Failed / In Progress)

- **Where:** `enum nvme_status_code` in `libnvme/src/nvme/nvme-types.h` —
  generic section ends at `NVME_SC_INVALID_PLACEMENT_HANDLE_LIST = 0x2A`
  (line 8509), then jumps to `NVME_SC_LBA_RANGE = 0x80` (line 8510).
- **Spec:** Figure 102 "Status Code – Generic Command Status Values"
  (spec.txt lines 9220–9280):
  - `0x2B` — Sanitize Namespace Failed
  - `0x2C` — Sanitize Namespace In Progress
- **Impact:** A controller that returns either of these in response to any
  command involving a namespace sanitize operation will be reported as
  "unrecognized" by `nvme_status_to_string`.

<ref_snippet file="/home/dennis/src/nvme-cli/libnvme/src/nvme/nvme-types.h" lines="8508-8520" />

---

### H3. Missing Cross-Controller Reset status codes (0x3F, 0x40, 0x41)

- **Where:** Command Specific section of `enum nvme_status_code` ends at
  `NVME_SC_CONTROLLER_DATA_QUEUE_FULL = 0x3B` (line 8593). The 2.3 spec
  adds three more codes in Figure 103 for the new **Cross-Controller Reset**
  Admin command (opcode 38h):
  - `0x3F` — Cross-Controller Reset in Progress
  - `0x40` — Cross-Controller Reset Log Page Full
  - `0x41` — Cross-Controller Reset Limit Exceeded
- **Impact:** Together with C1/C5/C10 above, this means the entire
  Cross-Controller Reset feature (one of the headline additions in 2.x) is
  effectively undecoded end-to-end: Identify fields missing, opcode
  missing, status codes unmapped, no decoders for logs 1Eh/1Fh.

<ref_snippet file="/home/dennis/src/nvme-cli/libnvme/src/nvme/nvme-types.h" lines="8586-8594" />

---

### H4. `generic_status[]` string table missing 0x29 and 0x2A

- **Where:** `libnvme/src/nvme/util.c` lines 206–257.
- **What's missing:** the array contains no entries for the values 0x29 and
  0x2A, even though both are present in the enum (lines 8508–8509):
  - `NVME_SC_FDP_DISABLED = 0x29`
  - `NVME_SC_INVALID_PLACEMENT_HANDLE_LIST = 0x2A`
- **Impact:** Errors returned by FDP-enabled controllers about disabled FDP
  or invalid placement-handle lists print as "unrecognized" rather than the
  spec text.
- **Fix:** add entries to the table at indices `[NVME_SC_FDP_DISABLED]` and
  `[NVME_SC_INVALID_PLACEMENT_HANDLE_LIST]` (using designated initialisers,
  consistent with surrounding code).

<ref_snippet file="/home/dennis/src/nvme-cli/libnvme/src/nvme/util.c" lines="240-257" />

---

### H5. `cmd_spec_status[]` string table missing several spec-defined entries

- **Where:** `libnvme/src/nvme/util.c` lines 259–316.
- **Missing values (from Figure 103):**
  - `0x2E` — Namespace Is Dispersed (Reservation Acquire/Register/Release/Report)
  - `0x35` — Invalid Host (Manage Exported NVM Subsystem)
  - `0x36` — Invalid NVM Subsystem (Manage Exported NVM Subsystem)
  - `0x3C` — Request Exceeds Max NS Sanitize Operations In Progress
  - `0x3D` — Manufacturing Default Personality Required (Firmware Commit)
  - `0x3E` — Invalid Power Limit (Set Features)
  - `0x3F`, `0x40`, `0x41` — Cross-Controller Reset codes (see H3)
- **Impact:** Same as H4 — these all turn into "unrecognized".

<ref_snippet file="/home/dennis/src/nvme-cli/libnvme/src/nvme/util.c" lines="295-316" />

---

### H6. FDP Configuration printer does not display `maxpids`

- **Where:** `nvme-print-stdout.c::stdout_fdp_configs` (line 1001) and
  `nvme-print-json.c::json_nvme_fdp_configs` (line 2512).
- **Spec:** Figure 287 "FDP Configuration Descriptor", bytes 11:10 —
  **Max Placement Identifiers (MAXPIDS)**: the maximum value allowed in
  the NPID field of an I/O Management Send Reclaim Unit Handle Update.
  The struct already has the field (`__le16 maxpids;` in
  `struct nvme_fdp_config_desc`), but neither printer emits it.
- **Impact:** Users running `nvme fdp configs` cannot see a spec-mandated
  field that they need to interpret valid NPID ranges.
- **Fix:** print `le16_to_cpu(config->maxpids)` in both functions.
- **Minor:** the stdout printer also doesn't print the `size` field;
  consider whether to surface that too.

<ref_snippet file="/home/dennis/src/nvme-cli/nvme-print-stdout.c" lines="1001-1032" />
<ref_snippet file="/home/dennis/src/nvme-cli/nvme-print-json.c" lines="2512-2558" />

---

### H7. Missing I/O Command Set Specific status code 0x84 (Invalid Command ID, SCT=1)

- **Where:** the NVM-status section of `enum nvme_status_code` in
  `libnvme/src/nvme/nvme-types.h` and `nvm_status[]` in `util.c` (line 318).
- **Spec:** Figure 104 "Status Code – Command Specific Status Values, I/O
  Commands", value `0x84` — **Invalid Command ID**.
- **Subtlety:** `NVME_SC_FORMAT_IN_PROGRESS = 0x84` exists at line 8514 but
  that one is a *Generic* (SCT=0) status from Figure 102. The I/O-CS
  Specific (SCT=1) value `0x84` is a different status entirely and has no
  enum entry or string mapping. Combined SCT+SC lookup is therefore
  ambiguous here.
- **Impact:** medium-to-high; affects I/O commands receiving SCT=1/SC=0x84.

<ref_snippet file="/home/dennis/src/nvme-cli/libnvme/src/nvme/util.c" lines="318-337" />

---

### H8. Async Event Information (Notice) — 9 spec-defined sub-values missing

- **Where:** `enum nvme_async_event_info_notice` in
  `libnvme/src/nvme/nvme-types.h` lines 6923–6930.
- **What's defined today:** values `0x00`–`0x06` and `0xF0` only — i.e. the
  NVMe-1.4-era set.
- **Spec:** Figure 154 "Asynchronous Event Information – Notice"
  (spec.txt lines 11506–11607) defines:
  - `0x07` — Reachability Group Change *(new in 2.x)*
  - `0x08` — Reachability Association Change *(new)*
  - `0x09` — Allocated Namespace Attribute Changed *(new)*
  - `0xEF` — Zone Descriptor Changed *(I/O CS specific)*
  - `0xF1` — Host Discovery Log Page Change *(new)*
  - `0xF2` — AVE Discovery Log Page Change *(new)*
  - `0xF3` — Pull Model DDC Request *(new)*
  - `0xF4` — Cross-Controller Reset Completed *(new)*
  - `0xF5` — Lost Host Communication *(new)*
- **Impact:** Any controller emitting these notices will appear to nvme-cli
  as an unknown event. The handful of new log pages whose only delivery
  mechanism is one of these notices (Reachability Groups, AVE Discovery,
  Cross-Controller Reset, Lost Host Communication) will never be triggered
  for collection.

<ref_snippet file="/home/dennis/src/nvme-cli/libnvme/src/nvme/nvme-types.h" lines="6920-6932" />

---

### H9. Live Migration / Track / Controller Data Queue commands: libnvme has helpers, CLI has no surface

- **Where:**
  - `libnvme/src/nvme/nvme-cmds.h` has `nvme_init_cdq_create()` (line 4939
    area), `nvme_init_cdq_delete()` (~4975), `nvme_init_lm_track_send()`
    (~5005), `nvme_init_lm_migration_send()` (~5083),
    `nvme_init_lm_migration_recv()` (~5138).
  - `nvme.c` / `nvme-builtin.h`: no `track-send`, `track-receive`,
    `migration-send`, `migration-receive`, or `controller-data-queue`
    subcommand. Grep for `track-send|migration-send|controller-data-queue`
    returns zero results.
- **Spec:** §5.2.4, §5.2.16, §5.2.17, §5.2.27, §5.2.28 — these are all
  mandatory commands for controllers that advertise Live Migration support
  (refer to OAES / OAQD-related bits in Identify Controller).
- **Impact:** Users have no way from the CLI to drive any of these
  commands; the libnvme implementations are reachable from C bindings but
  not from the shell. This is a coverage gap for any tester or operator
  who wants to exercise 2.3 migration flows.
- **Severity:** High (feature-completeness gap) rather than a correctness
  bug, but flagged because every other admin command this size has a CLI
  surface.

<ref_snippet file="/home/dennis/src/nvme-cli/libnvme/src/nvme/nvme-cmds.h" lines="4930-5145" />

---

### H10. Discovery Information Management (opcode 21h) / Send Discovery Log Page (39h) — opcode only, no payload structs, no CLI

- **Where:** Both opcodes are present in `enum nvme_admin_opcode`
  (`nvme_admin_discovery_info_mgmt = 0x21`,
  `nvme_admin_send_disc_log_page = 0x39`). No matching
  `nvme_init_discovery_info_mgmt()`, no payload data structures, no
  status-code entries for the Discovery Information Management subset of
  Figure 535, and no `nvme` subcommand.
- **Spec:** §5.4.4 (Discovery Info Mgmt) and §5.4.11 (Send Discovery Log
  Page) — required for Centralized / Direct Discovery Controllers.
- **Impact:** A user running nvme-cli against a CDC/DDC has no path to
  exercise these flows; the opcode constants are dead weight without
  surrounding plumbing.

---

### H11. Fabric Zoning command family: opcodes defined, no payload structs or CLI

- **Where:** `nvme_admin_fabric_zoning_recv = 0x22`,
  `nvme_admin_fabric_zoning_lookup = 0x25`,
  `nvme_admin_fabric_zoning_send = 0x29`,
  `nvme_admin_create_export_nvms = 0x2A`,
  `nvme_admin_clear_export_nvm_res = 0x28`,
  `nvme_admin_manage_export_nvms = 0x2D`,
  `nvme_admin_manage_export_ns = 0x31`,
  `nvme_admin_manage_export_port = 0x35` are all declared, but there are
  no `nvme_init_fabric_zoning_*` wrappers, no payload structs for
  ZoneGroup descriptors / Fabric Zone Entries (spec Figures 540–570 area),
  and no CLI surface.
- **Spec:** §5.4.5–5.4.10. Mandatory for Fabric Zoning controllers.
- **Impact:** Same pattern as H10 — opcodes without implementation. Down-
  graded from Critical because Fabric Zoning is optional for ordinary
  Fabrics controllers and only mandatory for CDCs / Zoning controllers.

---

### H12. Persistent Event Log: `unsafe_shutdowns` vs `unexpected_power_losses` rename
*(promoted out of M2 because of how widely-consumed this field is)*

- **Where:** `struct nvme_smart_log` at `libnvme/src/nvme/nvme-types.h`
  line 3816.
- **Spec:** Figure 210 "SMART / Health Information Log Page", bytes 159:144
  — **Unexpected Power Losses (UPL)**. The spec explicitly notes: *"Note
  that this field was previously named Unsafe Shutdowns."* — i.e. same
  field, formal rename in 2.x.
- **Impact:** functional behaviour is correct, but the struct field name,
  printer labels ("Unsafe Shutdowns:"), and JSON keys all use the old name.
  Many downstream tools key off the JSON keys.
- **Fix (suggested):** rename the struct field to `unexpected_power_losses`
  while keeping a `#define unsafe_shutdowns unexpected_power_losses` (or
  C union-alias) for ABI/API stability of downstream consumers, and update
  print labels — being careful that any user-visible JSON key change is
  documented as a compat break.

<ref_snippet file="/home/dennis/src/nvme-cli/libnvme/src/nvme/nvme-types.h" lines="3801-3830" />

---

### H13. Firmware Slot Info: AFI byte is not bit-decoded (CAFS / NAFS)

- **Where:** `nvme-print-stdout.c` line 4327, `nvme-print-json.c` line 627.
  Both print `afi` as a raw `%#x` byte.
- **Spec:** Figure 212 "Firmware Slot Information Log Page", byte 0 (AFI):
  - bits 2:0 — Current Active Firmware Slot (CAFS)
  - bit  3   — Reserved
  - bits 6:4 — Next Active Firmware Slot (NAFS) — *new in 2.x; indicates
    the slot that will be activated at the next Controller Level Reset.*
  - bit  7   — Reserved
- **Impact:** Users have to manually mask bits to learn which slot is
  currently active vs which is queued for activation. The "human" /
  verbose output should decode these sub-fields.

<ref_snippet file="/home/dennis/src/nvme-cli/nvme-print-stdout.c" lines="4320-4340" />

---

## MEDIUM

### M1. Various command-specific status codes only partially populated

- See H5 — every value mentioned there is *also* missing from
  `enum nvme_status_code` itself, not just from the string table. Listed
  here once at Medium because most are tied to features (Manage Exported
  Subsystem, Power Limit, etc.) that may or may not be exercised in
  practice. Recommend adding all of them in one patch alongside H5.

### M2. Async Event Information Immediate (Figure 156) and One-Shot (Figure 157) sub-values not enumerated in code

- The AET type values themselves are present (`NVME_AER_IMMEDIATE = 3`,
  `NVME_AER_ONESHOT = 4`), but there are no per-type sub-value enums.
- **Spec sub-values:**
  - Immediate (Figure 156): 0x00 = NVM Subsystem Normal Shutdown,
    0x01 = Temperature Threshold Hysteresis Recovery.
  - One-Shot (Figure 157): 0x00 = Controller Data Queue Tail Pointer,
    0x01 = Controller Data Queue Full Error, 0x02 = Power Threshold
    Exceeded.
- **Impact:** Decoders that pretty-print AERs cannot label the sub-value;
  they print a numeric code without context.

### M3. Identify CNS 1Ah (I/O CS specific Allocated NS list) — enum entry, no dedicated struct

- The CNS enum has `NVME_IDENTIFY_CNS_CSI_ALLOCATED_NS_LIST = 0x1A` at
  `libnvme/src/nvme/nvme-types.h:8938`, but no struct dedicated to it.
- **Spec:** Figure 340 says the response is "a Namespace List", which is
  the same as `struct nvme_ns_list`. Functionally usable, but no decoder
  routes CNS 1Ah specifically — `nvme id-ctrl --cns=0x1a` falls back to
  a generic list dump.

### M4. Log Page 1Ch (Changed Allocated Namespace List) — no dedicated decoder

- Spec §5.2.12.1.27 says the payload is a Namespace List (same shape as
  `struct nvme_ns_list`). The LID enum value exists, but there is no
  `stdout_changed_alloc_ns_list()` / `json_changed_alloc_ns_list()`
  routing in `nvme-print-stdout.c` / `nvme-print-json.c`. Falls back to
  the generic raw / hex print.

> *(Note: items M5/M6 from the original audit were promoted to High in
> the expansion pass — see H13 for AFI bit decoding and H12 for the
> SMART `unsafe_shutdowns` → `unexpected_power_losses` rename.)*

---

## LOW

### L1. Persistent Event Log header field names don't match spec abbreviations

- **Where:** `struct nvme_persistent_event_log` in
  `libnvme/src/nvme/nvme-types.h` lines 4467–4488.
- **Mismatches (code → spec):**
  - `rv` → `lrev` (Log Revision)
  - `ts` → `tstmp` (Timestamp)
  - `pcc` → `pwrcc` (Power Cycle Count)
  - `gen_number` → `gnum` (Generation Number)
- **Offsets and sizes are correct** (verified against Figure 230). This is
  purely a naming/readability issue — purposeful renames would touch a fair
  number of call sites, so this is Low priority. Documenting it here so the
  next person who touches the struct knows.

<ref_snippet file="/home/dennis/src/nvme-cli/libnvme/src/nvme/nvme-types.h" lines="4446-4490" />

---

### L2. Telemetry log: `ths`/`hostdgn`/`tcs` union is functional but confusingly named

- **Where:** `struct nvme_telemetry_log` at
  `libnvme/src/nvme/nvme-types.h` lines 4232–4251.
- **Background:** the Host-Initiated (LID 07h, Figure 218) and the
  Controller-Initiated (LID 08h, Figure 220) logs share the same 512-byte
  header *layout*, but two bytes differ in meaning:
  - Host log: byte 380 = THS (Host scope), byte 381 = THDGN (Host data
    gen number).
  - Controller log: byte 380 = Reserved, byte 381 = TCS (Controller
    scope).
- **What the code does:** declares byte 380 as `ths` and byte 381 as a
  `union { __u8 hostdgn; __u8 tcs; }`. The byte offsets are correct for
  both variants — but the `ths` field unconditionally claims to be "Host
  Scope" even when the struct is being used to decode a Controller-
  Initiated log. Likewise the `ctrlavail`/`ctrldgn` field names don't
  match the spec's TCDA / TCDGN abbreviations.
- **Impact:** Decoders that print `ths` for a Controller-Initiated log
  display a Host-scope value that is actually Reserved (spec mandates `0`).
  In practice this prints "0", which is harmless — but the field naming
  invites misuse in any future patch.
- **Suggested fix:** make byte 380 a union too (`union { __u8 ths;
  __u8 rsvd380; }`), and rename `ctrlavail`/`ctrldgn` to `tcda`/`tcdgn`
  to match the spec abbreviations.

> Note: a parallel sub-agent initially classified this as Critical
> (claiming the offsets are wrong). After spec re-verification they are
> not wrong — the layout works for both variants. Demoted to Low.

<ref_snippet file="/home/dennis/src/nvme-cli/libnvme/src/nvme/nvme-types.h" lines="4232-4251" />

---

### L3. Sanitize-status field-name spelling (`etond` vs spec ETODMM)

- Field semantics are identical; only the spec's abbreviation is more
  verbose. A future MNSOIP/STNSID patch (H1) is a natural time to align
  names.

---

## INFO / OBSERVATIONS (not bugs)

### I1. Log Page ID 0x19 is *transport-specific*, not base-spec defined

- `NVME_LOG_LID_PHY_RX_EOM = 0x19` in libnvme is fine — Figure 206 in the
  base spec leaves 0x19 to "the applicable NVM Express Transport
  specification", and PCIe-transport spec assigns it to PHY RX EOM. So
  libnvme defining a name for it is a layering convenience, not a base-spec
  violation.

### I2. The "WIP: CI fix" commit (07cf17ec9) is intentionally not for merge

- `git show 07cf17ec9` only touches `.github/workflows/run-nightly-tests.yml`:
  - Hard-codes `runs-on:` to `arc-vm-dennis-nvme-cli` (a personal runner).
  - Deletes the entire `- name: Run blktests in VM` step.
- This is a developer scratch commit, not a spec compliance issue. **It
  should not be merged in this state.**

### I3. Sanitize-status field-name spelling (`etond` vs spec ETODMM, etc.)

- `etond/etbend/etcend` in code correspond to ETODMM/ETBENMM/ETCENMM in
  Figure 302. Semantics are identical; only the abbreviation is more terse.
  Not a bug, but if you do an MNSOIP/STNSID patch (H1) it would be a good
  opportunity to align names.

### I4. Auth Send / Auth Receive are correctly routed via Fabrics (not Admin)

- A parallel sub-agent initially reported these as "missing" by grepping
  for `nvme_admin_auth_*`. They are correctly defined as Fabrics-command
  sub-types:
  `nvme_fabrics_type_auth_send = 0x05`,
  `nvme_fabrics_type_auth_receive = 0x06` (nvme-types.h ~9963).
  This matches the spec (§6.6/§6.7): all Fabrics commands share opcode
  `7Fh`, with the sub-type field discriminating between them.

### I5. Fabrics command-set status codes (Figure 105, 80h–91h) are fully present and stringified

- Verified during the expansion pass:
  - `NVME_SC_CONNECT_FORMAT=0x80`, `..._CTRL_BUSY=0x81`,
    `..._INVALID_PARAM=0x82`, `..._RESTART_DISC=0x83`,
    `..._INVALID_HOST=0x84`,
    `NVME_SC_DISCONNECT_INVALID_QTYPE=0x85`,
    `NVME_SC_DISCOVERY_RESTART=0x90`, `NVME_SC_AUTH_REQUIRED=0x91`.
  - All have corresponding entries in `nvmf_status[]` in
    `libnvme/src/nvme/util.c` lines 339–348.

### I6. Audit scope (post expansion)

For the requester's awareness, after the expansion pass these areas are
**still** not exhaustively checked against spec 2.3:

- ZNS / KV / Computational Programs Command Sets (companion specs).
- NVMe-MI command set internals (`mi.c`, `mi-types.h`) — only base-spec
  interactions covered.
- NVMe-over-Fabrics transport binding specifics beyond DLPE + TSAS.
- Fabric Zoning command *payload* structures (Figures 540–570) — opcodes
  audited, payload structs out of scope (see H11).
- Capacity Configuration Descriptor (Figure 262) deep layout.
- Migration Send/Receive Controller State data structure (Figure 374
  area) — sub-agent verified the *opcodes* are missing wrappers (H9) but
  did not exhaustively audit the spec-defined payload format.
- `Documentation/` man-page tree (for documentation drift).
- All vendor plugins (`plugins/wdc`, `plugins/ocp`, `plugins/sndk`, etc.)
  except `plugins/fdp` which was checked.
- I/O-CS Specific status codes `0x88`–`0x9C` (Computational Programs:
  Insufficient Program Resources, Invalid Memory Range Set, etc.) — these
  live in the NVM-CS / CP-CS specs rather than the base spec.

---

## VERIFIED OK during this audit

Listed for confidence-building — every item here was actually read in the
spec and checked field-by-field against the C struct:

**Identify variants (CNS):**
- CNS 03h — Namespace Identification Descriptor list (`nvme_ns_id_desc`).
- CNS 14h — Primary Controller Capabilities (`nvme_primary_ctrl_cap`).
- CNS 15h — Secondary Controller List (`nvme_secondary_ctrl`,
  `nvme_secondary_ctrl_list`).
- CNS 16h — Namespace Granularity List
  (`nvme_id_ns_granularity_desc`, `nvme_id_ns_granularity_list`).
- CNS 17h — UUID List (`nvme_id_uuid_list`, `nvme_id_uuid_list_entry`).
- CNS 18h — Domain List (`nvme_id_domain_attr`, `nvme_id_domain_list`).
- CNS 19h — Endurance Group List (`nvme_id_endurance_group_list`).
- CNS 1Ch — I/O Command Set data structure (`nvme_id_iocs`).
- CNS 08h — I/O CS Independent Identify NS
  (`nvme_id_independent_id_ns`), including new 2.x DISNS / RGRPID /
  MAXKT fields.

**Log pages with correct struct + decoder:**
- 06h — Device Self-test (`nvme_self_test_log`).
- 09h — Endurance Group Information (`nvme_endurance_group_log`).
- 10h — Media Unit Status (`nvme_media_unit_stat_log`,
  `nvme_media_unit_stat_desc`).
- 11h — Supported Capacity Configuration List
  (`nvme_supported_cap_config_list_log`, `nvme_capacity_config_desc`).
- 12h — Feature Identifiers Supported and Effects
  (`nvme_fid_supported_effects_log`).
- 14h — Command and Feature Lockdown (`nvme_lockdown_log`).
- 15h — Boot Partition (`nvme_boot_partition`).
- 16h — Rotational Media Information
  (`nvme_rotational_media_info_log`).
- 17h — Dispersed NS Participating NVM Subsystems
  (`nvme_dispersed_ns_participating_nss_log`).
- 18h — Management Address List
  (`nvme_mgmt_addr_desc`, `nvme_mgmt_addr_list_log`).
- 1Ah — Reachability Groups
  (`nvme_reachability_group_desc`, `nvme_reachability_groups_log`).
- 1Bh — Reachability Associations
  (`nvme_reachability_association_desc`,
  `nvme_reachability_associations_log`).
- 25h — Power Measurement (`nvme_power_meas_log` — 64-byte header +
  histogram descriptor array verified).
- 72h — AVE Discovery (`nvme_ave_discover_log`,
  `nvme_ave_discover_log_entry`, `nvme_ave_tr_record`).
- 73h — Pull Model DDC Request (`nvme_pull_model_ddc_req_log`).
- 70h / 71h — Discovery / Host Discovery (`nvmf_discovery_log`,
  `nvme_host_discover_log`).

**Persistent Event Log (Figure 230 header + per-event-type formats):**
- Header layout and 512-byte total size (offsets verified against spec
  Figure 230 byte-for-byte).
- Per-event structs verified by spec figure: SMART/Health snapshot,
  Firmware Commit, Timestamp Change, Power-on/Reset, NVM Subsystem HW
  Error, Change NS, Format Start/Completion, Sanitize Start/Completion,
  Set Feature, Telemetry Log Create, Thermal Excursion, Sanitize Media
  Verification (0Eh).
- Event Type enum values 01h–0Eh, DEh, DFh — all match spec. *(Missing
  0Fh is C4.)*

**Commands with correct opcode + libnvme wrapper + CLI exposure:**
- Capacity Management (20h) — full implementation, CDW10/11/12 layout
  verified against Figures 160–162.
- Lockdown (24h) — full implementation, CDW10/CDW14 layout verified
  against Figure 354.
- Format NVM (80h) — CDW10 fields including the new LBAFU (bits 13:12)
  are present in `enum nvme_format_*`.
- FW Commit (10h) — CA values 0–3, 6, 7 in `enum nvme_fw_commit_ca`
  match Figure 185 exactly.
- Sanitize (84h) — CDW10 incl. EMVS (bit 10), all SANACT values
  including `EXIT_MEDIA_VERIF = 5` match Figure 388.

**Status codes / enums verified compliant:**
- All Generic status codes `0x00`–`0x2A`, `0x80`–`0x89`.
- All Command-Specific Admin codes `0x00`–`0x3B`.
- All Media + Path codes (Figures 106/107).
- All Fabrics codes (Figure 105) — see I5.
- Admin Opcode enum 00h–8Ch except 0x38 (see C5).
- Log Page ID enum 00h–25h, 70h–73h, 7Fh–81h (with the caveats noted in
  C9 / C10 about missing structs for 1Dh, 1Eh, 1Fh).
- Feature ID enum 01h–25h, 78h–85h (all base-spec FIDs accounted for).

**Identify Controller (CNS 01h) layout verified for ALL non-CIU/CIRN fields:**
- Recent 2.x additions RHIRI, HIRT, CMMRTD, NMMRTD, MINMRTG, MAXMRTG,
  TRATTR, MCUDMQ, MNSUDMQ, MCMR, NMCMR, MCDQPC are present at correct
  offsets (568–587).
- Total struct size is exactly 4096 bytes.
- Endianness annotations correct throughout.
- The only structural bug is C1 (missing CIU/CIRN).

---

## Suggested patch ordering

If you only have time for a few patches, the rough cost/value order is:

1. **C2 + C3** — single-line `= 0x12,` / `= 0x15,` fixes, no ABI impact,
   removes a silent correctness footgun.
2. **H2 + H3 + H4 + H5 + H8** — pure additions to enums and string
   tables; minimal risk; large UX win on `nvme_status_to_string` and AER
   pretty-printing.
3. **H6** — two short `printf` / `obj_add_uint` additions.
4. **H1** — small struct change (3 new fields, split a reserved block);
   add decoders; touches `stdout_sanitize_log` / `json_sanitize_log` /
   binary printers.
5. **C5** — add `nvme_admin_cross_ctrl_reset = 0x38` to the opcode enum
   (one-line) and any matching CLI plumbing if desired.
6. **C1** — same shape as H1: split a reserved block, add fields, possibly
   add printers; broader because Identify is consumed by lots of code.
7. **C4** — new enum entry plus new struct plus a new decoder branch;
   moderate effort.
8. **C6 + C7 + C8 + C9 + C10** — adding new CNS / LID structs and
   decoders for the new-in-2.3 logs/identifiers. Each is a self-contained
   patch; tackle in any order. C10 (Cross-Controller Reset log) pairs
   naturally with C5 + H3 to fully wire up that feature.
9. **H9 + H10 + H11** — exposing existing libnvme helpers (where they
   exist) and writing new ones (where they don't) as CLI subcommands.
   Larger patches, but the libnvme implementations being in place makes
   this mostly UI plumbing.
10. **H12 + H13** — SMART field rename (with compat alias) and AFI
    bit-decoding (small).
11. **L1 / L2 / L3 / M1–M4** — cosmetic / readability / completeness
    cleanups.
