---
title: "Fantasy football"
description: "Your ESPN fantasy football matchup on the panel: set your league and your team, and the panel reads the score itself. Public leagues only."
sidebar:
  order: 5.5
---

The panel can follow your ESPN fantasy football team alongside the real games. Set your league and your team once, and the panel reads your matchup for the week itself, about every five minutes: your team's points against this week's opponent, with both teams' records.

## Set it up

On the [device page](/using/device-page/), open the **Teams** tab and find the **Fantasy football** card:

- **League ID** is the number in your league's web address, after `leagueId=`. On the league's home page the address reads like `fantasy.espn.com/football/league?leagueId=123456`.
- **Your team ID** is the number in your own team page's address, after `teamId=`.

Save, and the panel starts reading. The card then shows the matchup it read: the week, both team names and the points, like "Week 10: Alphabet Soup 104.18 — Xylophone Yetis 96.2", with **Final** once the week is over. On a bye week the card says your team is on a bye.

The two numbers are plain settings, like picking a favorite team: they are saved on the panel, they travel with a [settings backup](/using/backup/), and nothing else is needed. There is no password or sign-in anywhere in it.

## Public leagues only

This works because a public ESPN league can be read by anyone, the same way the panel reads real scores. A private league answers only to a signed-in member, and the panel has no ESPN account and never asks for one, so a private league's matchup can't be read. If your league is private, its commissioner can change it to public under the league's settings on ESPN's site; the panel picks it up on its next read.

## What shows where

The matchup shows on the device page's Fantasy football card, and apps that read the panel see it in the panel's state. The LED board itself keeps to the real games it follows.

To turn fantasy off, clear the League ID on the card and save.
