# SDIO Controller Verification with Synopsys VC-VIP-SOC eMMC/SD VIP

## 1. Purpose and scope

This report documents the integration of the Synopsys VC-VIP-SOC eMMC/SD card
Verification IP (VIP) as an additional, independent verification path for the
ZipCPU-derived SDIO controller inside `megasoc_chip`, alongside the existing
`mdl_sdio.v` behavioural-model flow and FPGA bring-up. It covers:

- why this verification was undertaken given FPGA validation had already passed
- the architecture of the integration
- how the verification was carried out
- every issue found, its root cause, and its resolution (or, where left open,
  why)
- what was learned
- whether any RTL change was required as a result

## 2. Why this verification, given FPGA already passed

FPGA bring-up and the existing `mdl_sdio.v` behavioural-model simulation had
both already validated the SDIO controller. Neither caught the issues
described in Section 5. This is not a contradiction -- it reflects a real
difference in what each verification method actually checks:

| Verification method | What it checks | What it cannot check |
|---|---|---|
| FPGA bring-up | Real silicon timing, real card interoperability | Whether the *protocol* was followed correctly when the card happens to tolerate the deviation |
| `mdl_sdio.v` (behavioural model) | Functional command/response sequencing | Real-time SD bus timing margins (it has no timing checker); does not implement CMD13 responses; does not track ACMD6 bus-width state |
| Synopsys VIP | Full SD Physical Layer Specification 4.20 protocol and timing compliance, with an independent, spec-derived checker | Nothing within its modelled scope -- this is its purpose |

Real SD cards are commonly tolerant of out-of-spec host behaviour (clocking
faster than negotiated, missing handshakes) because card vendors design for
the the install base of existing host controllers, not the letter of the
spec. A behavioural model with no timing checker is, by construction, equally
tolerant. The Synopsys VIP is deliberately *not* tolerant: it implements the
SD Physical Layer Specification 4.20 timing and protocol rules as hard
checks. This is exactly why it surfaced two real, previously-invisible
issues (Section 5.5, 5.7) that neither FPGA nor the behavioural model could
have caught, and why VIP-based verification has standalone value even after
FPGA sign-off.

## 3. Architecture

### 3.1 Principle: additive, not invasive

The integration adds a new, independent verification path. It does not
modify the RTL under `megasoc_chip/` or `megasoc_tech/logical/sdspi/rtl/`
(with one narrowly-scoped exception, see Section 5.4), and does not touch
`mdl_sdio.v` or its sibling behavioural-model files. The existing BEHAV flow
(`flist/project/top_BEHAV.flist`) continues to work unchanged, completely
independently of this new path.

### 3.2 New files

```
verif/testbench/logical/uvm_vip/
    tb_top.sv            top-level testbench: instantiates megasoc_chip_pads
                          unmodified, wires SDIO_CK/SDIO_CMD/SDIO_DAT[3:0] to
                          the VIP's svt_emmc_if, bridges UART-based
                          simulation-end detection into a UVM virtual
                          interface
    card_cfg.sv          SD/SDHC card configuration (card type, capacity,
                          CSD/SCR/OCR register fields) matching the
                          project's validated operating point
    sdio_card_env.sv      card-only UVM environment -- builds
                          svt_emmc_card_agent only; no host_agent field
                          exists anywhere in this class
    sdio_base_test.sv     base UVM test: builds the card-only env, applies
                          card_cfg, and drives the phase objection from the
                          embedded software's own completion signal (see
                          3.4)
    sdio_vip_tc3_test.sv  the TC3 (single-block read) test: backdoor-loads
                          known data into the card model and re-loads it
                          whenever the host issues CMD0 (see 5.6)
    sim_done_if.sv        tiny SV interface bridging a plain Verilog signal
                          into the UVM virtual-interface world

flist/project/
    uvm_vip_tb.flist      testbench filelist (parallel to megasoc_tb.flist)
    top_VIP.flist         top-level filelist (parallel to top_BEHAV.flist)
```

### 3.3 Topology: card-only VIP, real RTL as host

The defining architectural decision is that the VIP supplies only the
**card** side. The **host** is the real, unmodified ZipCPU SDIO controller
RTL inside `megasoc_chip`, driven by the same embedded C test software
(`megasoc_tech/software/src/sdio_tests/sdio_tests.c`, TC1-TC14) and the same
`sdiodrv.c` AXI-Lite driver already used by the BEHAV/FPGA flows. No new AXI
driving code was written. This was a deliberate, non-negotiable requirement:
it means the *exact* host hardware and software being shipped is what gets
exercised against the protocol-accurate card model, not a synthetic
stand-in.

Consequently:
- `sdio_card_env.sv` declares no `host_agent` field at all -- not merely an
  unused one. This makes it structurally impossible for two drivers to
  contend on `SDIO_CMD`/`SDIO_DAT`.
- `svt_emmc_card_agent` is reactive by design -- it never raises or drops a
  UVM phase objection itself, and never initiates a transaction. It only
  ever responds to what the real RTL host sends it.

### 3.4 Test completion: bridging real RTL into UVM

Because the host is plain RTL driven by embedded software, not a UVM
sequence, the test cannot use a UVM sequence's natural end as the signal to
finish. Instead:

- the embedded test software (`sdio_tests.c`) already calls `TEST_PASS()` /
  `TEST_FAIL()` at the end of its run, which calls `UartEndSimulation()`,
  which transmits a single `0x04` byte over the UART
- `megasoc_uart_capture.v` (existing testbench infrastructure, already used
  by every other test in this project) detects that byte and pulses its
  `SIMULATIONEND` output -- previously left unconnected in `megasoc_tb.sv`
- `tb_top.sv` connects that pulse to `sim_done_if.sv`, a one-bit SV
  interface
- `sdio_base_test::main_phase` raises its objection immediately and blocks
  on `sim_done_vif.done`, dropping the objection only once the embedded
  software has actually finished and reported pass/fail

This was necessary because a reactive card agent that never raises its own
objection, combined with naive UVM phasing, can produce a test that appears
to "pass" at simulation time zero, before the RTL has done anything at all.

### 3.5 Compile/run flow

The integration reuses the project's existing filelist-based VCS flow
(`flows/makefile.simulate`) rather than introducing a parallel build system.
Selecting the VIP path is a matter of overriding two Makefile variables that
were already designed to be overridable:

```bash
make compile_vcs \
  TBENCH_VC=$SOCLABS_PROJECT_DIR/flist/project/top_VIP.flist \
  DESIGN_VC=$SOCLABS_PROJECT_DIR/flist/project/top_VIP.flist \
  DEFINES_VC="<see Section 6>"

make run_vcs TESTNAME=sdio_tests \
  TBENCH_VC=$SOCLABS_PROJECT_DIR/flist/project/top_VIP.flist \
  DESIGN_VC=$SOCLABS_PROJECT_DIR/flist/project/top_VIP.flist \
  VCS_SIM_OPTION="... +UVM_TESTNAME=sdio_vip_tc3_test ..."
```

The BEHAV flow's invocation (`TBENCH_VC=top_BEHAV.flist`, no `DEFINES_VC`
override) is completely unaffected.

## 4. How the verification was carried out

1. **Environment discovery.** Located the actual VIP install
   (`/eda/synopsys/2022-23/RHELx86/VC-VIP-SOC_2022.12`, *not* a separate
   "DesignWare eMMC VIP" as initially assumed), confirmed VCS T-2022.06-SP2
   license compatibility, and mapped the project's real file layout (which
   differed from initial assumptions: RTL lives under
   `megasoc_tech/logical/sdspi/rtl/`, not a top-level `rtl/`).
2. **Standalone VIP validation.** Extracted the VIP's own reference example
   (`tb_emmc_svt_sd_uvm_basic_sys`) into a scratch directory outside the
   repo and ran it standalone with VCS, independent of this project's DUT,
   to confirm the VIP, the license, and the toolchain all work together
   before touching any project files.
3. **Card-only environment build.** Wrote the new files listed in Section
   3.2, validated the SD-pin mapping against the VIP interface's actual
   signal names, and got a first elaboration-only compile clean against the
   real `megasoc_chip` RTL.
4. **First real-traffic run.** Ran the full `sdio_tests` embedded software
   image (the same TC1-TC14 suite used by the BEHAV/FPGA flows) against the
   VIP card, iterating through the issues in Section 5 as each was exposed.
5. **Root-cause confirmation by direct signal capture**, rather than
   inference, at each step: a temporary, narrowly-time-windowed `$display`
   tap on the actual `SDIO_CK` pin, on the SDIO controller's AXI-Lite read
   data bus, and on the controller's internal busy/receive state machine,
   each added, used to settle one specific question, and removed once
   confirmed. None of these taps are part of the final integration.
6. **Final confirmation.** Full TC1-TC14 suite run to completion:
   `Total SDIO errors = 0`, `** TEST PASSED **`.

## 5. Issues identified and resolved

### 5.1 Pin tri-state merge (testbench bug, fixed)

**File:** `verif/testbench/logical/uvm_vip/tb_top.sv`

`SDIO_CMD`/`SDIO_DAT[3:0]` are open-drain tri-state nets (chip pad + pullup +
card all share one electrical node). The first wiring attempt connected the
VIP's `card_if` to these nets with reciprocal `assign` statements:

```verilog
assign SDIO_CMD       = card_if.cmd_di;
assign card_if.cmd_di = SDIO_CMD;
```

This adds two more unconditional drivers on top of the real ones instead of
sharing a single node, and was the root cause of the card agent observing no
bus traffic at all in the first run. Fixed using `tran` (the same primitive
this testbench already uses for its GPIO loopback), which correctly merges
the two nets into one shared tri-state node.

### 5.2 GNU Make `DEFINES_VC` override gotcha (build-flow learning, not a bug fix)

Overriding `DEFINES_VC` from the `make` command line silently **suppresses**
the top-level Makefile's own `DEFINES_VC += +define+CORTEX_A53
+define+USE_TARMAC +define+VERILATOR` line -- a command-line-set variable
blocks all assignments to it from the Makefile, including `+=`, unless the
Makefile uses `override`. This is standard GNU Make behaviour, not a project
bug, but it is easy to trip over: any `DEFINES_VC` override must explicitly
re-include those three defines, or VCS-driven simulation silently loses
`+define+VERILATOR` and reverts to the unrealistic 1 GHz behavioural
`CPU_CLK` described in 5.4.

### 5.3 VCS license/version check

Confirmed `VCS_2022.06-SP2`'s release train matches the VIP's recorded
`.lic_ver.txt` value (`2022.06`) before any compile was attempted, per the
project's licensing constraints.

### 5.4 `CPU_CLK` behavioural model frequency (RTL-directory file, but simulation-only; fixed, justification below)

**File:** `megasoc_chip/chip/logical/megasoc_clock_ctrl.v`

This is the one change made to a file under `megasoc_chip/`. It is justified
in detail in Section 7; the short version is that the changed file is a
non-synthesizable, simulation-only stand-in clock generator, structurally
separate from the real ASIC and FPGA clock generators, and the change makes
it match an assumption that was already documented elsewhere in the RTL.

The VIP's protocol checker reported the SD bus clock running at roughly 10x
the SD Physical Layer Specification's permitted frequency, both during card
identification (~3.97 MHz observed vs. a 400 kHz limit) and during data
transfer (~500 MHz observed vs. a 50 MHz limit) -- the same 10x factor in
both modes, pointing to one systematic cause rather than two unrelated
ones.

Root cause, confirmed by direct source inspection: `megasoc_clock_ctrl.v`'s
behavioural `CPU_CLK` generator free-ran at 1 GHz, completely independent of
its `CLK_IN` input:

```verilog
always @(clock_cpu)
    #0.5 clock_cpu <= !clock_cpu; // 1ns period, 1GHz
```

Meanwhile, `megasoc_tech/software/lib/sw_lib/devices/src/sdiodrv.c`'s
`SDIOCK_*` divisor constants, and `sdio.v`'s own header comment ("effectively
limiting our operation to 50MHz from a 100MHz clock"), both assume a 100 MHz
`CPU_CLK`. 1 GHz / 100 MHz = 10x -- the exact, precise ratio observed in
both measurements.

**Fix:** changed the free-running period from `#0.5` (1 GHz) to `#5` (100
MHz), matching the documented assumption exactly. No other change to the
file.

**Why this was not caught by the BEHAV flow:** `mdl_sdio.v` implements no
real-time SD bus timing checks at all, so it never noticed -- or reported --
that the simulated clock was 10x too fast. The bug was real and present in
every prior BEHAV-flow simulation; it was simply invisible to that flow by
construction.

### 5.5 SDHC density / CSD register configuration (VIP-config issue, fixed)

**File:** `verif/testbench/logical/uvm_vip/card_cfg.sv`

The VIP's own protocol checker enforces the SD Physical Layer Specification's
rule that an SDHC card must declare a density of at least 2 GB. The card
configuration's CSD register fields were left at their uninitialised
defaults, declaring a density of roughly 512 KB, which the VIP correctly
rejected as a configuration error (not an RTL/test failure). Fixed by
populating the CSD v2.0 fields with values giving a multi-gigabyte density,
following the same field-by-field pattern as the VIP's own reference
example. The SCR register's `SD_SPEC`/`SD_SPEC4` fields were also corrected
to the encoding the VIP expects for `emmc_spec_ver = SD_4_20` (the reference
example's own values were for a different `emmc_spec_ver` and triggered an
SCR-consistency warning here).

### 5.6 Card memory cleared on CMD0 (VIP/test-environment interaction, fixed)

**File:** `verif/testbench/logical/uvm_vip/sdio_vip_tc3_test.sv`

A single backdoor pre-load of the test data, done once at
`start_of_simulation_phase` (before any simulation time has advanced), was
found to be wiped before the embedded software's CMD17 read -- because the
real RTL host sends CMD0 (GO_IDLE_STATE) multiple times during its normal SD
initialisation handshake, and the VIP's card model clears its data array on
CMD0. This is standard, spec-correct SD card behaviour, not a VIP bug.
Fixed with a small UVM subscriber (`tc3_cmd0_repoke_subscriber`) connected to
the card monitor's transaction-observed analysis port, which re-applies the
backdoor pattern every time a CMD0 is observed, regardless of how many times
it occurs.

### 5.7 Backdoor memory is block-addressed, not byte-addressed (the actual root cause of TC3 returning all-zero data; fixed)

**File:** `verif/testbench/logical/uvm_vip/sdio_vip_tc3_test.sv`

This was the central, most consequential finding, and the reason TC3
initially returned all-zero data despite every other part of the chain
(pin wiring, clock, card configuration, CMD0 re-poke) being correct.

The VIP's underlying memory model logs its configuration on creation:

```
new memserver instance created: ... 2-state adr-bits=36 data-bits=4096 ...
```

`data-bits=4096` means the memory's native addressable word is **4096 bits =
512 bytes = exactly one SD block**, not one byte. (This is also why the
reference example's testbench defines `+define+SVT_MEM_MAX_DATA_WIDTH=4096`
-- a detail copied from the reference example without, at first, registering
its significance.)

The original backdoor pre-load code called `poke()` 512 times, once per
byte, at addresses `sector*512 .. sector*512+511`:

```systemverilog
for (int unsigned i = 0; i < TC3_BLOCK_SIZE; i++)
  void'(mem_back_door.poke(TC3_BYTE_ADDR + i, TC3_PATTERN_BYTE));
```

Under block-addressed memory, this pokes **512 separate blocks**, each with
only its lowest byte set to the test pattern and the remaining 511 bytes
defaulting to zero -- and never touches block `0x1000` itself, which is what
the real CMD17 (whose argument is the raw sector number for an SDHC card)
actually reads. The peek-after-poke self-check that was meant to catch
exactly this kind of mistake instead "passed", because it checked the same
(wrong) addresses that were poked -- internally consistent, but never
validated against an actual SD read.

This was confirmed, not guessed: a direct, time-windowed `$display` tap on
the SDIO controller's AXI-Lite read-data bus during the live CMD17 window
showed `RDATA=0x00000000` for every one of the 128 words read, both before
and after every other fix in this report had already been applied.

**Fix:** poke one 4096-bit-wide value (built from 512 copies of the test
byte) at address `TC3_SECTOR` directly -- one `poke()` call, not 512.
Re-confirmed by the same AXI-Lite tap: `RDATA=0xc3c3c3c3` for all 128 words.

### 5.8 `sdiodrv.c` stale-status-read (investigated, NOT the cause, change reverted)

While investigating 5.7, the busy-wait loop in
`sdio_wait_while_busy_timeout()` was found to have a real, separate logic
issue: its "wait for data transfer to complete" phase checked a status value
left over from the previous phase before re-reading it, rather than
re-sampling first. This is a legitimate latent bug -- under the right timing
it could let a caller proceed before a transfer has actually started -- but
direct RTL state-machine tracing proved it was **not** the cause of the
all-zero CMD17 reads (the busy/done state machine was already completing
correctly, with timing matching a real 512-byte/4-bit/50 MHz transfer to the
nanosecond). Since fixing it was not required to resolve this verification's
actual issue, and it touches software shared with the real FPGA/hardware
target, the change was reverted in full; `sdiodrv.c` is back to its
pre-existing state (which, separately, already contains substantial,
unrelated PYNQ FPGA bring-up work from earlier debugging that this
verification did not touch).

### 5.9 Missing intermediate print lines (observed, unresolved, non-blocking)

During debugging, some of the embedded software's intermediate `printf`
lines (e.g. `Test4:`/`Test5:`/`Test6:` headers) were observed to be missing
from the captured console log on some runs, while later lines (`TC6 executed
successfully` and beyond) printed correctly and the suite's final result was
unaffected. Several explanations were investigated and ruled out
specifically: it is not a kill-induced log-flush artifact (reproduced on a
naturally-progressing, non-killed run), not a UART clock-rate mismatch (the
driver's own `CLKFREQ=100000000` assumption already matches the corrected
`CPU_CLK`), and not a software stdio-buffering issue (`printf` is `#define`d
to a custom, fully unbuffered `c_print()` that calls the low-level
`UartPutc()` per character with no buffering layer). The root cause was not
pinned down further once the actual data-correctness bug (5.7) was found and
fixed, after which the full suite passed end-to-end including the final
`** TEST PASSED **` print. This is logged here as an open, low-priority,
purely cosmetic item, not a verification blocker.

### 5.10 CMD6 high-speed mode never negotiated (open finding, not fixed -- driver gap, not RTL)

The VIP's protocol checker continues to report the SD bus clock running at
50 MHz while the card remains in `DEFAULT_SPEED` mode (whose specification
limit is 25 MHz), because `sdiodrv.c` selects 50 MHz directly after the
ACMD6 bus-width switch without ever issuing CMD6 (SWITCH_FUNCTION) to
formally negotiate `HIGH_SPEED` mode first. The RTL is doing exactly what it
is told -- the measured 50 MHz clock is correct and intentional -- so this
is a **protocol-negotiation gap in the embedded driver**, not an RTL defect.
It was not fixed as part of this verification effort; see Section 8 for the
recommendation. It is exactly the kind of out-of-spec host behaviour that
real SD cards often tolerate silently (which is why FPGA bring-up did not
surface it) and that the VIP, by design, does not.

## 6. How the Synopsys VIP is connected, and the mandatory steps to reproduce this

### 6.1 Locating the VIP

```bash
echo $DESIGNWARE_HOME   # was empty; do not assume a path
find /eda -maxdepth 6 -ipath "*vip*emmc*"
```

On this system, the install is:
```
/eda/synopsys/2022-23/RHELx86/VC-VIP-SOC_2022.12
```
This must be exported as `DESIGNWARE_HOME` for every compile and run:
```bash
export DESIGNWARE_HOME=/eda/synopsys/2022-23/RHELx86/VC-VIP-SOC_2022.12
```

### 6.2 Compile-time requirements

The VIP's `SVT_SOURCE_MAP_*` macros need `DESIGNWARE_INCDIR` defined to
resolve directly against the install tree (rather than requiring a
`dw_vip_setup`-generated local design directory):

```
+define+DESIGNWARE_INCDIR=$DESIGNWARE_HOME
```

VCS needs its bundled UVM library enabled, and the VIP needs its own
technology/packer defines:

```
-ntb_opts uvm
+define+SVT_UVM_TECHNOLOGY
+define+SYNOPSYS_SV
+define+UVM_DISABLE_AUTO_ITEM_RECORDING
+define+UVM_PACKER_MAX_BYTES=150000
+define+SVT_MEM_MAX_DATA_WIDTH=4096
```

(`SVT_MEM_MAX_DATA_WIDTH=4096` is not cosmetic -- see Section 5.7. It sets
the width of `svt_mem_data_t`, which must be wide enough to hold one full
block-addressed memory word.)

The VIP's backdoor memory feature requires its common library linked at
compile time:
```
$DESIGNWARE_HOME/vip/svt/common/latest/C/lib/linux64/libmemserver.so
```

The project's own base defines must be carried explicitly in the same
override (see Section 5.2 for why):
```
+define+CORTEX_A53 +define+USE_TARMAC +define+VERILATOR
```

### 6.3 Full compile and run commands

```bash
cd /home/ssrs1c25/megasoc_project
source set_env.sh
export DESIGNWARE_HOME=/eda/synopsys/2022-23/RHELx86/VC-VIP-SOC_2022.12

make testcode TESTNAME=sdio_tests

make compile_vcs \
  TBENCH_VC=$SOCLABS_PROJECT_DIR/flist/project/top_VIP.flist \
  DESIGN_VC=$SOCLABS_PROJECT_DIR/flist/project/top_VIP.flist \
  DEFINES_VC="+define+CORTEX_A53 +define+USE_TARMAC +define+VERILATOR -ntb_opts uvm +define+DESIGNWARE_INCDIR=$DESIGNWARE_HOME +define+SVT_UVM_TECHNOLOGY +define+SYNOPSYS_SV +define+UVM_DISABLE_AUTO_ITEM_RECORDING +define+UVM_PACKER_MAX_BYTES=150000 +define+SVT_MEM_MAX_DATA_WIDTH=4096 $DESIGNWARE_HOME/vip/svt/common/latest/C/lib/linux64/libmemserver.so"

make run_vcs TESTNAME=sdio_tests \
  TBENCH_VC=$SOCLABS_PROJECT_DIR/flist/project/top_VIP.flist \
  DESIGN_VC=$SOCLABS_PROJECT_DIR/flist/project/top_VIP.flist \
  VCS_SIM_OPTION="+vcs+lic+wait +vcs+flush+log -assert nopostproc +UVM_TESTNAME=sdio_vip_tc3_test +UVM_VERBOSITY=UVM_LOW"
```

Logs:

| What | Path |
|---|---|
| Main simulation log | `simulate/sim/sdio_tests/logs/run_sdio_tests.log` |
| Embedded software UART capture | `simulate/sim/sdio_tests/logs/uart.log` |
| Compile log | `simulate/sim/build/compile_vcs.log` |
| Waveform | `simulate/sim/sdio_tests/novas.fsdb` |

### 6.4 Pin-level connection

`svt_emmc_if`'s signals map directly onto `megasoc_chip`'s existing
`SDIO_CK`/`SDIO_CMD`/`SDIO_DAT[3:0]` pins (the same pins `mdl_sdio.v`
connects to in the BEHAV flow):

| `svt_emmc_if` signal | `megasoc_chip` pin | Direction note |
|---|---|---|
| `clk_sclk` | `SDIO_CK` | host-driven only; plain `assign` |
| `cmd_di` | `SDIO_CMD` | open-drain, bidirectional; `tran` |
| `dat0_rclk_plus` | `SDIO_DAT[0]` | open-drain, bidirectional; `tran` |
| `dat1_rclk_minus_irq` | `SDIO_DAT[1]` | open-drain, bidirectional; `tran` |
| `dat2_rw_irq` | `SDIO_DAT[2]` | open-drain, bidirectional; `tran` |
| `cd_dat3_cs` | `SDIO_DAT[3]` | open-drain, bidirectional; `tran` |

## 7. Final justification: was any RTL change required?

**No.** Every issue that needed fixing to make this verification pass was
either:

- in the new, additive testbench/VIP-configuration files written for this
  integration (Sections 5.1, 5.5, 5.6, 5.7) -- none of these are RTL, and
  none of them affect the BEHAV flow, FPGA, or ASIC builds in any way, or
- in the simulation-only behavioural clock model (Section 5.4), which is
  textually inside `megasoc_chip/chip/logical/` but is not RTL in the sense
  that matters for this question: it is a non-synthesizable testbench
  construct (`always @(clock_cpu) #0.5 clock_cpu <= !clock_cpu;`), and is
  one of three independent, separately-maintained implementations selected
  by which top-level filelist is used --

  | Filelist | File used | Target |
  |---|---|---|
  | `top_ASIC.flist` | `megasoc_chip/chip/logical/ASIC/megasoc_clock_ctrl.v` | real ASIC |
  | `top_FPGA.flist` | `megasoc_chip/chip/logical/FPGA/megasoc_clock_ctrl.v` | real FPGA |
  | `top_BEHAV.flist` / `top_VIP.flist` | `megasoc_chip/chip/logical/megasoc_clock_ctrl.v` | behavioural simulation only |

  The `ASIC/` and `FPGA/` variants were not touched and are unaffected. The
  fix only corrects the *simulated* clock rate to match a frequency
  assumption that is already documented in `sdio.v` and baked into
  `sdiodrv.c`'s own divisor constants -- it does not change what frequency
  any real chip or FPGA target actually runs at, because neither of those
  targets ever used this file in the first place.

The one functional gap this verification surfaced that genuinely reflects a
real-world correctness issue -- CMD6 high-speed negotiation never being
performed (Section 5.10) -- is in `sdiodrv.c`, not RTL, and remains open by
choice (not fixed in this engagement; see Section 8).

## 8. Recommendation: CMD6 high-speed negotiation

`sdiodrv.c` should issue CMD6 (SWITCH_FUNCTION, function group 1, value 1)
to negotiate `HIGH_SPEED` mode before selecting a 50 MHz clock, and should
fall back to `DEFAULT_SPEED`'s 25 MHz limit if that negotiation is not
performed or fails. This is a software change, not an RTL change, and is
recommended as follow-up work, not included in this verification effort.
Until it is made, the driver's current behaviour is functionally working
(confirmed against both the VIP and real cards) but is not strictly SD
Physical Layer Specification 4.20 compliant, which is precisely the kind of
gap VIP-based verification exists to find.

## 9. Learnings

- **A protocol-accurate VIP and a behavioural model check fundamentally
  different things.** Neither one is more "correct" in an absolute sense;
  they validate different layers (real-time bus compliance vs. functional
  command sequencing), and both have value. FPGA bring-up validates a third,
  different thing again (real silicon, real cards, but only the specific
  cards on hand, and only the timing margins those specific cards happen to
  need).
- **Self-checks must validate against the thing they're meant to protect,
  not against themselves.** The peek-after-poke self-check in Section 5.7
  was logically sound and still missed the bug, because it checked the same
  (wrong) addresses it had just written -- it needed to be checked against
  an independent ground truth (the actual CMD17 read) to be meaningful.
- **A memory model's own log output can contain the answer.** The
  `data-bits=4096` line, printed automatically when the VIP's memory backend
  is created, was the direct, literal answer to "why is this all zero" --
  available from the very first run, well before the multi-hour signal-level
  debugging that eventually arrived at the same conclusion independently.
- **GNU Make command-line variable overrides are not additive with `+=`
  unless the base value came from somewhere other than the command line.**
  This is generic Make behaviour, not specific to this project, but it had a
  real, confusing effect here (Section 5.2) and is worth remembering for any
  future flag overrides on this build system.
- **When something works on real hardware/FPGA but not in a stricter
  simulation environment, look for what the stricter environment checks that
  the looser one does not -- the gap is usually informative, not spurious.**
  Every issue in this report followed that pattern.
