# Options: where each switch lives in the game

Every option in the iOS options sheet is a switch the shipped game already has. The port only sets it, once per
frame, before `nativeRender` (`harness/cheats.c`). Addresses are link addresses in `libLEGO_M1.so` 1.05.2 (sha256
`0125c976…69b3`). `tests/c/test_cheats.c` checks every option by booting the real game in the emulator and calling
the game's own query functions.

## The developer cheat byte nobody calls

`SaveGame_DoCheats(SAVEGAMEDATA*)` (0x1980c0) has **no callers** in the shipped build. It still shows what the
`g_CheatOptions` bytes (0x4451c0) meant to the developers. Its readers:

| Bit | Read by | Effect | Option |
|---|---|---|---|
| byte 0, `0x08` | `SaveGame_IsCharUnlocked`, `IsCharHubUnlocked`, `IsCharBought` | every character unlocked and bought | All characters |
| byte 0, `0x10` | `SaveGame_IsCharacterSuperMove(X)Bought` | every super move bought | All super moves |
| byte 0, `0x20` | `UIFrontEnd_IsLevelUnlocked` | every level selectable | All levels |
| byte 0, `0x40` | `SaveGame_IsExtraUnlocked`, `IsExtraBought(MP)` | every red-brick extra collected and bought | All red-brick extras |
| byte 1, `0x04` / `0x08` | `SaveGame_DoCheats` only | studs / prize tokens set to 10,000,000 | (not used, dead code) |
| byte 2, `0x04` / `0x08` | `SaveGame_DoCheats` only | +100,000 / +300,000 studs once (the old in-app purchases) | (not used) |

Nothing in the game writes these bits, so clearing them again restores the game's own answer. The save data is
never touched.

## Red-brick extras

`Extras_Active` (0x451910) is a bit mask; `Extras_IsActive(n)` tests bit `n`. The extras' names live in compressed
text files, so their meaning was read from what each caller does:

| Extra | Caller | Effect | Option |
|---|---|---|---|
| 1 | `GOCharacter_RegenerateHealth` | health regenerates by twice the amount | Faster healing |
| 3 | `GOPlayer_InitPlayers` | adds to the player's maximum health at level start | Extra hearts |
| 5 | `GO_DefaultHit` | hits by the player do double damage | Double damage |
| 7 / 8 / 9 | `Extras_ApplyStudMultiplier` | studs ×2 / ×4 / ×6, first active one wins | Stud multiplier |

The multiplier is one choice in the sheet because the game tests 7, then 8, then 9. The port clears only the bits
it set itself, so extras switched on in the game's own Extras menu stay on.

## Save fields

`SaveGame_Data` (0x4458e0) is the live `SAVEGAMEDATA`:

- `+0x78`, 64-bit: studs (what `SaveGame_DoCheats` writes with byte 1 bit `0x04`). **Never run out of studs** keeps it at
  1,000,000 or more.
- `+0x4a8`, 32-bit: prize-draw tokens (the prize picker decrements it per draw and shows it top left).
  **Unlimited prize draws** keeps it at 10 or more.

Both are stored with the game's next save.

## Invincibility

There is no invincibility extra. `GOPlayer_Active` (0x53dfe0) points at the player object; `+0x7c` is its character
data with the health at `+0xd0` (`GOCharacter_GetHealth`), and the maximum sits at `[+0x164] + 0x3b8`
(`GOCharacter_SetHealth` clamps to it). **Invincible** refills lost health on the next frame and calls
`Hud_SetHeartCount`, because the hearts display only updates through that function. A player already at 0 is
left to the game's own respawn.

## A bug that was game design

On the prize draw screen the back button seemed broken. `UI_PrizePicker_Module`'s touch handler only accepts back
in state 4 and plays a "denied" sound otherwise. After a free draw the result screen is state 7: the game wants
"play" (continue) first, then back works, and the game saves at that moment.
