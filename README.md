# ReFrame

ReFrame is a fork of [LibreVR/Revive](https://github.com/LibreVR/Revive) with a single goal: running Oculus PC VR games on a **Steam Frame** through SteamVR, using Revive's OpenXR backend (ReviveXR). Revive and all the work behind it belong to its original authors. Their complete commit history is kept in this repository, and the project stays under the original MIT license. ReFrame is not affiliated with Valve or Meta.

The work lives on the `steamframe` branch. First Contact and Lone Echo already run on a Steam Frame, with image, sound and controller input. The changes on top of Revive include requesting OpenXR 1.0, which SteamVR accepts, and swapchain fixes for formats and mip levels that SteamVR does not provide. They also include deploying the input manifests that SteamVR needs to treat the game as the running application, and pointing the play area forward towards the player at launch. To use them, enable **Use OpenXR runtime** in the Revive tray menu; the setting is remembered. The development log, written in Hungarian, is in [DEVLOG.md](DEVLOG.md).

The original Revive README follows.

---

# Revive Compatibility Layer

This is a compatibility layer between the Oculus SDK and OpenVR/OpenXR. It allows you to play Oculus-exclusive games on your HTC Vive or Valve Index.

[Refer to the wiki](https://github.com/LibreVR/Revive/wiki) if you run into any problems. You can also find a [community-compiled list of working games on the wiki](https://github.com/LibreVR/Revive/wiki/Compatibility-list), feel free to add your own results.

## Installation

*Always check the [compatibility list](https://github.com/LibreVR/Revive/wiki/Compatibility-list) before making a purchase.*

1. Download and install [Oculus Rift Software](https://www.oculus.com/en-us/setup/). When you get to "Select Your Headset", choose to "Skip".
2. [Download the latest Revive installer.](https://github.com/LibreVR/Revive/releases/latest)
3. Install Revive in your preferred directory.
4. Start SteamVR if it's not already running.
5. Put on the headset, open the dashboard and click the new Revive tab.
6. If you run into any problems, read the known issues below or refer to the [wiki](https://github.com/LibreVR/Revive/wiki).

## Known Issues

- Newly installed applications may refuse to start when you try to launch them for the first time, [simply follow these instructions to fix it](https://github.com/LibreVR/Revive/wiki/Troubleshooting#im-getting-an-entitlement-error-or-oculus-rift-not-found) or reboot your PC.
- If you don't see the Revive tab, go to the start menu on your desktop and start the Revive Dashboard. Or check the Applications tab in the SteamVR settings to see if the tab is enabled.
