---
title: "Wi-Fi"
description: "The networks the panel joins, the up to five it remembers, its setup hotspot, what it does when Wi-Fi drops, and its .local address."
sidebar:
  order: 15
---

The panel needs Wi-Fi to read scores from ESPN. Once it's on your network, it needs nothing else running.

## Use 2.4 GHz

The panel only joins 2.4 GHz networks. If your router has separate names for 2.4 GHz and 5 GHz, pick the 2.4 GHz one.

Use your main network, not a guest network. A guest network can let the panel join but keep your phone from reaching it afterwards, so the app and the device page can't find it.

## Saved networks

The panel remembers up to five Wi-Fi networks, like your home, a phone hotspot and a friend's house. At start-up it joins whichever saved network it can see, trying the last one that worked first. Set it up on a phone hotspot for a tailgate and it still knows your home network when you get back.

The Device card on the device page lists the saved networks under **Wi-Fi networks**, in the order the panel tries them, with **Connected** beside the one it's on, or **Tried first** beside the top one. **Forget** takes a network off the list. If the panel is on that network now, it stays connected until it next restarts.

When the list is full, adding a sixth network drops the one at the bottom. The panel always keeps one network to join, so forgetting the last one doesn't take it off Wi-Fi. [Reset Wi-Fi](/using/buttons-and-resets/#reset-wi-fi) and [Factory reset](/using/buttons-and-resets/#factory-reset) clear them all.

:::note[Coming in the next release]
Reset Wi-Fi keeps the saved networks. Only Factory reset clears them.
:::

## Adding a network

Every network you set the panel up on is added to the top of the list. There are two ways:

- **The Game Day app** finds the panel over Bluetooth and hands it the network and password. While the panel joins, it shows **JOINING** and the network's name. See [The Game Day app](/get-started/the-app/).
- **The panel's hotspot.** While it shows **SETUP**, the panel starts its own Wi-Fi network named **Game Day** and six letters and numbers, like **Game Day 2f6a70**. The screen shows the name. Join it from any phone or laptop and a setup page opens. Pick your Wi-Fi network, enter the password and save.

The six characters are the same as in the panel's address, so two panels in one house are easy to tell apart. See [Set up your panel](/get-started/setup/).

Taking the panel somewhere new? When it can't find any saved network, it says **Wi-Fi lost** and the app can reach it over Bluetooth, as below, so you can give it the new network there. To put it into setup straight away, use [Reset Wi-Fi](/using/buttons-and-resets/#reset-wi-fi).

## When Wi-Fi drops

A short drop, like a mesh system moving the panel to another access point, doesn't show. After 10 seconds without Wi-Fi the panel covers the board with **Wi-Fi lost** and the QR code for the Game Day app, so a stale score isn't left up.

- After 30 seconds, the panel turns Bluetooth back on so the Game Day app can reach it. A changed router password is the common case: open the app and give the panel the new password.
- After a minute without its network, a panel that was on it earlier restarts itself once, to recover. It never restarts while the app is connected to it.
- Without the app, join the panel's **Game Day** hotspot as above. It comes up within a few minutes of the panel losing Wi-Fi.

When the network comes back, the panel rejoins and the board comes back by itself.

:::note[Coming in the next release]
When the panel starts up and can't find its Wi-Fi, as after a power cut while the router is still starting, it says **Waiting for** the network and **Just had a power cut? / Routers take a few min** for its first 3 minutes, then **Wi-Fi lost**. Bluetooth and the hotspot come on at the usual times meanwhile.

On a bar, hotel or dorm Wi-Fi that lets the panel join but then wants you to sign in on a web page, the panel says **Wi-Fi needs sign-in, use home or a hotspot**, with the QR code for the app. The panel can't sign in to those pages, so set it up on a home network or a phone hotspot. The message goes away by itself once scores come through.

The device page's Diagnostics count every Wi-Fi drop and its cause, and draw the last day of signal. See [The device page](/using/device-page/#the-device-card).
:::

## The panel's address

Every panel has a name on your network, `gameday-` and six characters, like `gameday-2f6a70`. Its device page is at that name plus `.local`, like `gameday-2f6a70.local`. The panel shows the address when it joins Wi-Fi, and again when you hold the boot button for about 1.5 seconds. The device page shows it in the Device card under **Address** and **Name**.

Some phones can't open `.local` addresses. Use the panel's IP address instead, a number like `192.168.1.50`. The panel shows it with the address on a boot button hold, and the ticker gives it until you pick a team.

The panel treats your home network as trusted. Anyone on your Wi-Fi can open its device page and change its settings, and it isn't designed to be reached from the internet, so don't forward a port to it.
