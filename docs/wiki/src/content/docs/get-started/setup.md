---
title: "Set up your panel"
description: "Plug in the panel, put it on your Wi-Fi with the Game Day app or its own hotspot, and find its device page."
sidebar:
  order: 2
---

You need a phone and your Wi-Fi password. It takes a few minutes.

## Plug it in

Connect the power supply that came with the panel. The panel lights up with **GAME DAY** across the top and **Starting** under it.

A panel that isn't on Wi-Fi yet then shows **SETUP** and a QR code. On the 128x64 panel the QR code sits on the left half and the words **SETUP / Game Day / app** sit beside it. On a single 64x64 panel there is no room for both, so the words and the QR code take turns: about 5 seconds of words, then 8 seconds of QR code.

The QR code opens `https://app.gameday-scoreboard.com/setup`. On an iPhone, point the Camera at it: it opens the Game Day app if you have it, or the setup page in your browser if you don't.

## Set it up in the Game Day app

Open the [Game Day app](/get-started/the-app/). It finds the new panel over Bluetooth and lists it as a new panel nearby.

1. Tap the panel to set it up.
2. Pick your Wi-Fi network from the list. The list comes from the panel itself, strongest network first, so it shows what the panel can actually reach.
3. Enter the password.

While the panel joins, it shows **JOINING** and the name of the network the app sent it. If that's the wrong network, you can see it right there. Once the panel is on your Wi-Fi, the app lets you pick your team.

## Without the app: use the panel's hotspot

While it shows SETUP, the panel also starts its own Wi-Fi network, named **Game Day** plus six letters and numbers, like `Game Day 2f6a70`. Once the hotspot is up, the panel takes a turn showing **Join WiFi / Game Day** and those six characters, so you know which network is yours. Two panels in one house each get their own name.

1. On any phone or laptop, join that network. A setup page opens.
2. Pick your Wi-Fi network and enter the password.
3. Save.

The panel joins your Wi-Fi. Then [pick your team](/get-started/pick-your-team/) on its device page.

## Use 2.4 GHz Wi-Fi

The panel only joins 2.4 GHz networks. If your router has separate names for 2.4 GHz and 5 GHz, pick the 2.4 GHz one.

Use your main network, not a guest network. A guest network can let the panel join but keep your phone from reaching it afterwards.

:::note[Coming in the next release]
Some bar, hotel and dorm Wi-Fi lets the panel join and then wants a sign-in on a web page before anything else gets through. The panel can't sign in to those pages. On a network like that it says **Wi-Fi needs sign-in, use home or a hotspot** with the QR code for the app. Set it up on a home network or a phone hotspot instead. The message goes away by itself once scores come through.
:::

## What the panel shows once it's online

Right after it joins your Wi-Fi, the panel shows the QR code for the Game Day app. On the 128x64 panel the words **Scan for Game Day app** sit beside it. After about 13 seconds the panel's address takes a 5 second turn: **Open gameday-2f6a70.local**, with your panel's own six characters.

That address is the panel's device page. The six characters are the same ones in the hotspot's name. Open the address in a browser on any phone or computer on the same Wi-Fi to pick your team and change settings. See [The device page](/using/device-page/).

A new panel keeps the QR code and the address up for 45 seconds, or until you pick a team. A panel that already has a team shows them once, for about 18 seconds, every time it starts. Then it says **Looking up scores** while the first scoreboard loads, for up to 10 seconds.

Until you pick a team or a mode, the ticker along the bottom says **Pick a team in the Game Day app or at** followed by the panel's IP address, like `192.168.1.50`. Use the IP address if your phone won't open a `.local` address.

To see the address again any time, hold the button on the controller for about 1.5 seconds and let go. The panel shows its full address and IP address for 20 seconds. More on the button in [Buttons and resets](/using/buttons-and-resets/).

:::tip
To skip the QR code and the address when the panel starts, turn off **Show how to connect when the panel starts** in the Setup card on the device page. The panel then goes straight to the scoreboard.
:::

## New router or Wi-Fi password

When the panel loses its Wi-Fi for more than about 10 seconds, it covers the score and says **Wi-Fi lost / open app** next to the QR code for the app. A shorter drop, like a mesh system moving the panel to another access point, never covers the score.

1. Open the Game Day app. About 30 seconds into the outage the panel turns Bluetooth back on, so the app can find it.
2. Give it the new network or password, the same way as the first time.

If the app hands the panel a wrong password while it's in an outage, the panel goes back to the network it had. Without the app, join the panel's **Game Day** hotspot as above. It comes up within a few minutes of the panel losing Wi-Fi.

A panel that can't get back on its Wi-Fi restarts itself once, a minute into the outage. It never does that while you're connected to it from the app.

## The panel remembers up to five networks

Every network you set up is added to a list of up to five. Set the panel up on a phone hotspot for a tailgate, and it still knows your home network when it gets back. When it starts, it joins whichever saved network it can see, trying the last one that worked first.

The Device card on the device page lists the saved networks under **Wi-Fi networks**. **Connected** marks the one in use, and **Tried first** marks the one it tries first. **Forget** takes a network off the list.

**Reset Wi-Fi** and **Factory reset** on the device page clear the whole list. See [Wi-Fi](/using/wifi/) for more.

:::note[Coming in the next release]
**Reset Wi-Fi** keeps the saved networks. The panel restarts into setup for 15 minutes so you can give it a new network. If you unplug it and plug it back in, or the 15 minutes run out, it goes back to the networks it already knows. While it's in setup, the panel takes a turn saying **Unplug it to rejoin saved Wi-Fi**. **Factory reset** still erases everything.
:::
