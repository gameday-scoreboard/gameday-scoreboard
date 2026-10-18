---
title: "The boot button and resets"
description: "What a press or a hold of the controller's button does, and what Reset Wi-Fi and Factory reset keep and clear."
sidebar:
  order: 14
---

The controller has one button, the boot button. A quick press turns the panel on or off. Holding it shows the panel's address or resets it, so you can get back in without the app or the device page.

## Presses and holds

| Press or hold | What happens |
| --- | --- |
| Quick press | Turns the panel on or off. |
| Hold about 1.5 seconds | Shows the panel's `.local` address and its IP address for 20 seconds, then goes back to the board. Nothing is reset. |
| Hold 5 seconds | The screen says **Keep holding to reset Wi-Fi**, with a bar filling up. Let go before 10 seconds and nothing is reset. |
| Hold 10 seconds | The panel resets Wi-Fi while you hold it, says **Wi-Fi reset, restarting** and restarts into setup. See [Reset Wi-Fi](#reset-wi-fi). |

The address hold is the quickest way to find the panel again. See [Wi-Fi](/using/wifi/).

:::note[Coming in the next release]
Nothing resets until you let go, and the button gains a factory reset. Hold it and watch the screen:

| Hold | The screen says | Let go then and |
| --- | --- | --- |
| About 1.5 seconds | The address and IP | nothing is reset; the address stays up for 20 seconds |
| 5 to 10 seconds | **Keep holding**, **Wi-Fi reset**, counting down, with a bar filling | nothing is reset |
| 10 to 20 seconds | **Release to reset Wi-Fi**, and how long to hold to erase all | the panel resets Wi-Fi |
| 20 to 30 seconds | **Release to erase all**, and how long to hold to cancel | the panel does a factory reset |
| Past 30 seconds | **Release to cancel** | nothing is reset |

Holding the button on a panel that is off turns it on, so you can see the countdown.
:::

## Reset Wi-Fi

Reset Wi-Fi is for a new router, a new Wi-Fi password, or moving the panel to another network. Start it with the 10 second hold, or with **Reset Wi-Fi** in the Device card on the device page.

The panel forgets every Wi-Fi network it has saved and restarts into setup, showing **SETUP** and the QR code for the Game Day app, and starting its own hotspot. Set it up again from the app or the hotspot; see [Set up your panel](/get-started/setup/).

Your team, favorites and every other setting stay as they were.

:::note[Coming in the next release]
Reset Wi-Fi keeps the saved networks. The panel restarts into setup mode for 15 minutes, so you can put it on a new network from the Game Day app or its hotspot. The setup screen also says **Unplug it to rejoin saved Wi-Fi**: unplug the panel and plug it back in, and it goes back to the networks it already knows. It does that by itself after the 15 minutes too, or a little later if you're still setting it up from the app or the hotspot.
:::

## Factory reset

Factory reset erases everything: the saved Wi-Fi networks, your team, your favorites and every setting, including a paired WizMote and the panel layout. The panel restarts into setup the way it came out of the box, with the layout its firmware starts on. Do this before you sell or give the panel away.

Start it from **Factory reset** in the Device card on the device page. The page asks you to confirm first.

:::note[Coming in the next release]
With no app and no device page, hold the boot button until the screen says **Release to erase all**, at 20 seconds, then let go. If you change your mind, keep holding past 30 seconds until it says **Release to cancel**.
:::

## What each reset keeps

| | Reset Wi-Fi | Factory reset |
| --- | --- | --- |
| Saved Wi-Fi networks | Cleared | Cleared |
| Team, mode and favorites | Kept | Cleared |
| Settings (ticker, brightness, timezone and the rest) | Kept | Cleared |
| WizMote pairing | Kept | Cleared |
| Panel layout | Kept | Back to the firmware's starting layout |
| Firmware version | Kept | Kept |

If the panel won't start at all, a reset can't help. See [Troubleshooting](/help/troubleshooting/).
