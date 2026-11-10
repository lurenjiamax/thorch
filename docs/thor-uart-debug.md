# Thor debug UART source evidence

Checked 2026-11-10. Source inspection only: no pad probing, wiring, GPIO writes,
bootloader flashing or boot-argument changes were performed.

## Maintained Linux Thor tree

Thor includes `kernel-src/arch/arm64/boot/dts/qcom/qcs8550-ayn-common.dtsi`:

- `aliases.serial0 = &uart7`, `chosen.stdout-path = "serial0:115200n8"`;
- board override `&uart7 { status = "okay"; }`;
- UART14 is Bluetooth and UART15 is the gamepad, not the debug console.

`kernel-src/arch/arm64/boot/dts/qcom/sm8550.dtsi` defines:

| Item | Definition |
| --- | --- |
| Controller | `uart7`, GENI debug UART |
| MMIO | `0x00a9c000`, size `0x4000` |
| Compatible | `qcom,geni-debug-uart` |
| TX | TLMM `gpio26` |
| RX | TLMM `gpio27` |
| Pin function | `qup1_se7` |
| Default format | 115200 baud, 8 data bits, no parity, 1 stop bit |
| Linux console | `ttyMSM0` through the `serial0` alias and GENI console driver |

The TX/RX order is explicitly documented in the pinctrl source comment, not
inferred from GPIO numbering. These are SoC signal identifiers, not physical
connector pins or PCB test-point labels.

The built thorch45 configuration includes `CONFIG_SERIAL_EARLYCON=y`,
`CONFIG_SERIAL_QCOM_GENI=y`, and `CONFIG_SERIAL_QCOM_GENI_CONSOLE=y`. Its driver
registers `OF_EARLYCON_DECLARE(qcom_geni, "qcom,geni-debug-uart", ...)`.
Thus an already compiled kernel has the needed driver; hardware access and
diagnostic boot arguments remain separate requirements.

## Android reference, with provenance limitation

The public Android QCS8550 device-tree repository
[`onelots/android_kernel_ayn_qcs8550-devicetrees`, branch `lineage-23.2`](https://github.com/onelots/android_kernel_ayn_qcs8550-devicetrees/tree/lineage-23.2)
also defines this controller and these pins:

- [`qcom/kalama-qupv3.dtsi`](https://github.com/onelots/android_kernel_ayn_qcs8550-devicetrees/blob/lineage-23.2/qcom/kalama-qupv3.dtsi#L68):
  `qupv3_se7_2uart`, address `0xa9c000`, `qcom,geni-debug-uart`.
- [`qcom/kalama-pinctrl.dtsi`](https://github.com/onelots/android_kernel_ayn_qcs8550-devicetrees/blob/lineage-23.2/qcom/kalama-pinctrl.dtsi#L1):
  TX explicitly uses `gpio26` / `qup1_se7_l2`; RX uses `gpio27` /
  `qup1_se7_l3`.

The Android SoC node defaults to `status = "disabled"`; actual enablement depends
on the selected board overlays. This community Android reference is not proof
of the Thor production firmware's exact DTBO or physical routing. No matching
local full Android Thor kernel source checkout was located during this check.

## Capturing startup output

The imported Linux loader template currently selects `console=tty0` and does
not itself add `console=ttyMSM0` or `earlycon`. The native boot publisher's debug
mode removes `quiet` and adds `no_console_suspend`, but does not add those serial
arguments. A DT `stdout-path` alone is not a guarantee of serial printk output
when another console was selected on the command line.

After verifying the hardware UART wiring/ownership, diagnostic arguments to
evaluate separately include:

```text
console=ttyMSM0,115200n8 earlycon=qcom_geni,0xa9c000 ignore_loglevel
```

Preserve other required boot arguments and distinguish this diagnostic change
from the controlled compiler comparison. UART clocks/pinmux/firmware ownership
must be appropriate; `earlycon` does not guarantee logs before its own setup or
before the kernel entry point. Local `OpenABL/Abl/openabl.patch` also contains
`DisableHypUartLogging()` with a comment about freeing UART from hypervisor
logging on older firmware. Do not invoke its SMC speculatively.

## Electrical limitations

No PCB pad location, connector pinout, or UART signal voltage was established.
GPIO numbering and pin drive-strength do not establish voltage. Do not assume
3.3 V compatibility, use 5 V UART or RS-232 levels, or connect adapter VCC to the
board. First identify ground and TX, establish voltage from documentation or
measurement, and use a compatible receiver/level shifter. Initial capture can
be receive-only: board TX to adapter RX plus common ground, leaving board RX
unconnected.
