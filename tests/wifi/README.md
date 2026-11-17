# Wi-Fi connection checks

Run from the repository root:

```sh
python3 tests/wifi/run.py
python3 tests/wifi/run_ui.py /tmp/stopwatch-wifi-ui
```

The first command compiles the production network controller against deterministic
ESP-IDF fakes. It checks scan/exit connection preservation, cancellation and late
IP events, authentication failure, DHCP timeout, success-only credential storage,
background reconnect, open/unsupported networks, Wi-Fi switching and Badge mode.
It requires a native C++17 compiler.

The second builds the repository's real LVGL and Wi-Fi view with a fake HAL. It
checks keyboard/case/symbol input, masking, explicit confirmation, failure/retry,
open-network input, checkmark positioning and view destruction. It also renders
466×466 circular-screen previews as PPM files in the output directory. It requires
CMake and native C/C++ compilers. Neither test writes device settings or uses RF.

## Device acceptance

After building and flashing with the project's normal ESP-IDF workflow:

- Connect a WPA2/WPA3 personal network; verify the checkmark appears only after IP
  acquisition and remains when refreshing or leaving/reopening the list.
- Exercise the keyboard near the circular screen edges, including spaces,
  uppercase letters, backspace, and `^~\`|` symbols. Keyboard OK must not connect.
- Try an incorrect password, unavailable AP, and a router with DHCP disabled;
  verify an explanatory Close dialog and the 20-second overall timeout.
- While connected to A, cancel switching to B before and during connection;
  verify A stays connected or is restored. Failed B credentials must not replace A.
- Connect an open network; verify no password field is shown. Enterprise and other
  unsupported authentication types must produce a Close dialog.
- Reboot to verify the latest successful network reconnects. Remove/restore the
  router signal and verify silent background reconnection and checkmark removal.
- Toggle Wi-Fi off/on and enter/exit Badge edit mode; verify ownership transfers
  without hangs and the saved station reconnects afterwards.

Firmware build and host tests do not replace these radio/touch/power-cycle checks.
