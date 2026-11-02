# 7. Add a Lift

A lift is a sector whose floor moves. It starts level with a high ledge. When you press **E** on it,
it comes down, waits for you to step on, and carries you up.

```
   Seen from the side:

                          +--------+---------------------
                          |  Lift  |  Ledge (floor 40)
   -----------------------+  floor +---------------------
     Room (floor 0)       |  40    |
   -----------------------+--------+

   Press E: the lift comes down to floor 0.
   Step on and wait: it rises back to 40.
```

## Build one

**First, a room to reach.** The player can only step up 6 units, so a floor at 40 is out of reach
without the lift.

1. Start with an ordinary room with its floor at `0`. Make sure its ceiling is high, at `96` or more,
   so there's headroom on the way up.
2. Press **R**, then **2**. Leave a gap of 32 beyond the room, then draw another room beyond the gap.
3. **Right-click the far room.** Set its **Floor Height** to `40` and its **Ceiling Height** to `96`.
   This is your ledge.

**Now the lift itself.**

4. Draw a rectangle in the gap, 32 wide and 32 deep. It must touch both rooms.
5. **Right-click it.** Name it `My Lift`, set its **Floor Height** to `40` and **Ceiling Height** to `96`.
   That's the same as the ledge: the lift starts at the top.
6. Click **Make Lift...**
7. Choose how it moves, under **Motion**:
   - **Down, Wait, Up** is the classic Doom lift. Choose this one.
8. Choose what sets it off:
   - Tick **Use Its Walls**, so pressing **E** on the lift's wall works.
   - Leave **Step On It** and **On Level Start** off for now.
9. Click **Make Lift**.

Press **P**. Walk to the lift, which now looks like a raised wall in front of you, and press **E**. It
comes down. Step on, wait a few seconds, and it takes you up to the ledge.

## Make it look like a lift

The wall below the lift is what the player sees. Right-click that wall, the one between your room and
the lift, and drag a texture onto its **bottom** field. `PLAT1` from `Textures/Walls` is the classic
choice. Give the lift's floor `STEP2` from `Textures/Flats`, so it stands out.

To see it in Level 1, right-click **Lift**.

## Getting back down

Since the player can walk off the ledge and drop, a one-way lift is fine in many levels. To also bring
the lift down from the top, tick **Step On It** in step 8: then stepping onto the raised lift from the
ledge sends it down too.

## Other ways it can move

| Motion | What it does |
|---|---|
| **Down, Wait, Up** | Comes down, waits, goes back up |
| **Up, Wait, Down** | Goes up, waits, comes back down |
| **Back and Forth** | Never stops moving. Tick **On Level Start** with it. |
| **Down and Stay** | Comes down once and stays there |
| **Up and Stay** | Goes up once and stays there |

**Down and Stay** is also handy without a lift in mind: it can lower a bridge or reveal a pit.

## Finer control

Make Lift creates ordinary settings that you can change later. Right-click the sector and open its
**Lift** dispatch to change the speed, how long it waits and how far it goes. **Goes Down To** decides
where it stops: by default, the lowest floor next to it.

## If something goes wrong

| What you see | What to check |
|---|---|
| Nothing happens when you press E | Get closer and look at the lift's wall, not at the room behind it. |
| The lift doesn't come down far enough | It stops at the lowest floor next to it. Check the rooms touching it, or set a fixed **Low Height** in the Lift dispatch. |
| You hit your head on the way up | Raise the lift's ceiling. |
| The lift goes the wrong way | Check that its floor starts at the *high* position, as in step 5. |

---

Next: [Components: what things are made of](08-Components.md)
