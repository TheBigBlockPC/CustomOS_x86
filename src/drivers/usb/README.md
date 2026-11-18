# Small polling USB keyboard driver

This driver supports one USB 1.x/2.0 HID **boot-protocol keyboard**, connected
to any PCI xHCI controller, either directly or through USB 1.x/2.0 hubs. It
initializes every controller independently, scans at boot, skips other device
types, and keeps the first supported keyboard. Nested hubs are supported through
the USB limit of five external hubs. USB3 hubs can carry keyboards through their
USB2 companion function; SuperSpeed endpoints are not enumerated. An xHCI controller
can operate an ordinary low/full/high-speed USB keyboard; the keyboard itself
does not need to be a USB 3 device.

The existing `usb_init()` call in `kernel.c` runs after PCI and HPET initialization.
The existing Linux build already lists `usb.c` and `xhci.c`. This implementation
keeps the existing kernel and input subsystem interfaces unchanged. This
extension is confined to `src/drivers/usb/`; no build or kernel edits are needed.

## How it works

1. **Take over the host.** `xhci.c` iterates PCI class `0c:03:30`, maps each BAR0,
   takes ownership from firmware, resets each controller, and reads its USB2 port
   ranges. One failed host does not prevent trying the others. Registers are
   uncached MMIO; DMA buffers are ordinary RAM pages.
2. **Start DMA rings.** A command ring holds instructions to the controller.
   An event ring receives completions written by the controller. The device
   context base address array (DCBAA) points to each addressed device's context,
   which describes its endpoints. Each host owns its rings, slot table and DMA limits;
   each device owns its contexts and transfer buffers. Scratchpad buffers are
   allocated when required.
   Ring entries are 16-byte Transfer Request Blocks (TRBs). Cycle bits distinguish
   new entries after a wrap; barriers publish descriptors before doorbells.
   A No-Op command verifies that the command/event path works before enumeration.
3. **Enumerate a root-port device.** The driver debounces and resets the port,
   enables a slot, and issues Address Device. xHCI sends SET_ADDRESS itself.
   `usb.c` reads the device descriptor, adjusts endpoint zero's packet size,
   and scans configuration descriptors for class 3, subclass 1, protocol 1.
   It sends SET_CONFIGURATION, SET_PROTOCOL(boot), and SET_IDLE(changes only).
4. **Walk hubs.** A hub interface on alternate setting zero is configured before
   its hub descriptor is read. The driver powers its ports, waits for power good,
   clears change flags, debounces and resets connected ports, and recursively
   enumerates children. Each hub stays addressed while children use separate
   slots. Slot contexts carry the root port, route string, and high-speed
   transaction translator (TT) slot/port information, including TT inheritance
   through full-speed hubs. Multi-TT hubs use their default single-TT interface.
   Rejected children are disabled and their slots freed before scanning siblings.
5. **Receive key reports.** The keyboard's interrupt-IN endpoint gets one queued
   receive buffer. Although USB calls this an "interrupt" transfer, CPU interrupts
   are disabled in this host driver: xHCI schedules USB transactions and the CPU
   polls completion events. An idle keyboard can NAK indefinitely. Each completed
   receive is copied before its DMA buffer is queued again. Hub status control
   requests dispatch keyboard completions too, so polling a hub cannot discard
   input. The entire hub path is checked every 100 ms, from root to leaf, to
   detect keyboard or intermediate hub removal without querying a detached hub.
6. **Deliver input.** `poll_events()` polls both PS/2 and USB. A boot report has
   one modifier byte, one reserved byte, and six key usages. Comparing reports
   produces presses and releases. USB usages are translated to physical Set-1
   positions, then the input subsystem applies its existing German character
   tables and calls the existing TTY handler. Separate PS/2 and USB state prevents
   one keyboard from releasing another keyboard's held Shift. Software repeat
   starts after 400 ms and repeats every 40 ms while the input loop runs.

Control requests are synchronous and bounded to one page. Commands and control
transfers have one-second deadlines. A timeout stops the controller and retains
its pages rather than risking late DMA into freed memory. Rejected devices'
DMA pages are freed only after Disable Slot completes. For controllers without
64-bit addressing, pages above 4 GiB are rejected; there is no new low-memory
allocator. All calls belong on the bootstrap CPU in foreground context.

## Intentional limits

No reconnect enumeration, simultaneous keyboard aggregation, USB3
endpoints, bulk/isochronous drivers, MSI/IRQ delivery, generic HID report parsing,
LED updates, or additional layout features. Configurations are limited to 4096
bytes and scratchpads to 512 pages. Navigation/keypad/GUI keys are not translated;
Caps Lock and AltGr character behavior follow the existing input subsystem.
The keyboard must be connected at initialization. Detach or controller failure
releases held keys and disables USB keyboard input until reboot.

## Testing

Run:

```sh
python3 src/drivers/usb/test_keyboard.py
python3 tests/run_foundation.py
python3 tests/run_timer_host.py
```

The USB test builds temporary boot images and leaves production images alone.
It needs clang/lld, QEMU, and `/usr/share/OVMF/OVMF_{CODE,VARS}_4M.fd`.
Host checks run with AddressSanitizer/UndefinedBehaviorSanitizer, exercising
malformed/truncated descriptors, rollover, duplicate/unknown usages, simultaneous
modifiers, mixed PS/2/USB state, repeat, and detach. QEMU tests disable the PS/2
controller to prove USB input works, and cover no host, an empty host, a keyboard,
and a mouse before a keyboard at 128 MiB/8 GiB. They inject real keys through QMP
and verify the input subsystem, Backspace editing, TTY command submission, more
than 255 reports (ring wrap), repeat, and detach. Additional cases cover a second
controller with an empty/unsupported first host, external and nested hubs,
unsupported and empty hub siblings, switched port power, five-hub depth, and
keyboard/intermediate-hub detach. Synthetic DMA completion tests cover both
context sizes, high-speed TT routing and inheritance, independent controller
slots/failure/DMA limits, and keyboard events arriving during hub commands.
Headless test kernels replace the display-copy timer callback with an empty
callback. These checks validate
emulated hardware; physical controllers and firmware quirks are untested.

References: [Intel xHCI specification](https://www.intel.com/content/dam/www/public/us/en/documents/technical-specifications/extensible-host-controler-interface-usb-xhci.pdf),
sections 4.2/4.3/4.5/4.9 and 6.2/6.4;
[USB HID specification](https://www.usb.org/sites/default/files/hid1_11.pdf),
appendix B (boot keyboard protocol).
