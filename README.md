# mod-world-randomizer

A Wrath (3.3.5) take on "I randomized all of WoW, then did hardcore":

- **Brackets**: you list the level brackets (`Randomizer.Brackets`, lowest first, following on from each other).
  Every outdoor zone gets a home bracket from its real level range, then the brackets are shuffled between zones.
  Elwynn Forest might be 70-80 and Icecrown 1-10. By default neighbouring zones get neighbouring brackets
  (`Randomizer.Brackets.Adjacent`), so the levels flow across the map like the original world does.
- **Creatures**: every outdoor combat creature becomes a random creature whose level fits the zone's new bracket.
  The pool is every hostile / neutral creature that spawns somewhere in the open world, sorted into the brackets
  by level and split into land / water and normal / elite. All spawns of one creature in a zone become the
  same random creature, so packs stay packs.
- **Kept creatures** (quest kill targets, spawns with their own scripts) move their level into the new bracket.
- **Dungeons** move every creature's level by as much as their entrance zone's bracket moved.
- **New characters** start at a graveyard in a bracket 1 zone of their own faction (then contested zones), with
  the hearthstone set there. Death Knights keep Ebon Hold.
- **Player stats era**: base stats per level from Wrath (AzerothCore), TBC (cMaNGOS-TBC) or Vanilla
  (vMaNGOS), with an XP rate per era (Wrath x0.5, TBC x0.75, Vanilla x1 by default).

- **Hardcore** (optional): one life, chosen by the player (`.hardcore on confirm`) or forced for every new
  character. A dead hardcore character is never deleted for good: it is soft deleted with the core's own unlink
  (or locked, or left a ghost), and `world_randomizer_hardcore` keeps it with its owner account, level, killer and
  date. A bugged death is undone with `.hardcore restore <name or GUID> [reason]`.

Nothing is written to `creature`: the swaps happen in memory at startup. `Randomizer.Enable = 0` and a
restart bring the normal world back. The seed is kept in the characters DB, so the world stays the same
across restarts until you reroll it.

## Web pages (`web/`)

Plain HTML + JavaScript, no build step.

- `index.html`: character roller. Era, faction, race, class, spec, two professions; tick which factions, races and
  classes may be rolled (Death Knights are off by default, they start at 55); hardcore mode locks the first roll. It also tells you which bracket 1 zones you can start in with the seed from the map page.
- `map.html`: the world map with your aowow zone outlines, coloured by bracket. Original / Shuffled view,
  seed, bracket list, scope, and the config lines to paste into the server.

`web/randomizer.js` is the same shuffle as `WorldRandomizer.cpp` (same generator, same order), so a seed shows
exactly the world the server will build. Tested for 72 seed / scope / balance / adjacency combinations against the
C++.

The map uses aowow's map images. Copy `web/` into `aowow/Stuff/randomizer/` (or anywhere under your aowow) and
open `http://localhost/aowow/Stuff/randomizer/map.html`; the default image folder
`../../static/images/wow/maps/enus/normal/` then points at aowow's own. Without the images the map shows the
outlines on a plain background.

## Tables

World DB:

- `world_randomizer_zone`: the 64 outdoor zones from your aowow extract (cities left out), original level
  range, continent, expansion, territory. `enabled = 0` drops a zone; `shuffle = 0` keeps it in its own bracket.
- `world_randomizer_zone_link`: which zones border each other, worked out from the aowow outlines (plus a few
  borders across water and boat links), for `Randomizer.Brackets.Adjacent`.
- `world_randomizer_instance`: optional entrance zone per instance, if the area trigger lookup gets one wrong.
- `world_randomizer_spawn_zone`: cache. Most `creature` rows have `zoneId = 0`; the first start works their
  zones out from the terrain (slow once) and keeps them here.
- `world_randomizer_levelstats` / `world_randomizer_classlevelstats`: era 1 Vanilla (vMaNGOS world database
  `world_full_14_june_2021`) and era 2 TBC (cMaNGOS `mangos-tbc` `sql/base/mangos.sql`).

Characters DB:

- `world_randomizer_state`: the world seed.
- `world_randomizer_hardcore`: every hardcore character, owner account, status, level, killer, death date.
- `world_randomizer_hardcore_revive`: every restore, with the GM and the reason.

`tools/generate_from_aowow.py <aowow>/Stuff/zone-extract` rebuilds the zone SQL and `web/zones.js` after a new
aowow setup.

## Commands

| Command | Who | What |
|---|---|---|
| `.randomizer info` | players | Seed, stats era, and your zone's old and new bracket |
| `.randomizer zones` | GM | Every zone and its bracket |
| `.randomizer reroll [seed]` | admin | Store a new seed; the world changes after a restart |
| `.randomizer start` | GM | Teleport to a bracket 1 start point for your faction |
| `.hardcore` | players | Your hardcore status |
| `.hardcore on confirm` | players | Go hardcore (Mode 1, up to `OptInMaxLevel`) |
| `.hardcore fallen` | players | The last 10 hardcore deaths |
| `.hardcore restore <name or GUID> [reason]` | GM | Undo a death; lists the GUIDs when several dead characters share the name |

### Hardcore restore

```
.hardcore restore Hardxammy
Found multiple characters with this name:
- Hardxammy / GUID 741158 / Account 54394 / Hardcore (soft deleted) / died on 2026-10-20 at level 25
- Hardxammy / GUID 724665 / Account 54394 / Hardcore (soft deleted) / died on 2026-10-15 at level 32
Run it again with the GUID: .hardcore restore <GUID> [reason]

.hardcore restore 741158 died to a falling-through-the-world bug
Restored: Hardxammy / GUID 741158 / Account 54394 / Hardcore (soft deleted) / died on 2026-10-20 at level 25
```

The character goes back on its owner account under its old name, comes back to life on its next login, and the
restore is logged in `world_randomizer_hardcore_revive` (who, when, why). If the old name is taken by then, the
command says so; restore it with `.character deleted restore <GUID> <new name>` and it still counts as a restore
on its next login.

## About the stats eras

The era tables really are the base stats of those cores, but they are close to AzerothCore's: on average
Vanilla is 2.6 stat points away (total over all five stats) and TBC 8.8, and base health / mana differs on a
minority of levels. Most of what made the eras feel different was gear, talents and spells, which this does not
change. Races and classes an era didn't have are filled in: Blood Elf and Draenei in Vanilla use another race's
numbers plus AzerothCore's racial difference, and Death Knights use the Warrior's numbers. Past the era's level
cap (60 / 70) stats keep AzerothCore's growth from there.

## With mod-dynamic-scaling

Both modules pick creature levels in the same hook. If both run, turn off instance level scaling in
mod-dynamic-scaling (`DynamicScaling.Instance.LevelScaling.Enable = 0`) and either drop the open world zone rows
there or regenerate them from the shuffled brackets, or the two will pull levels in different directions.

## Known limits

- Quests keep their own levels and rewards; only their kill targets move levels.
- Replaced creatures keep the spawn's waypoints, mounts and auras from `creature_addon`.
- Dungeon trash and bosses keep their entries (boss scripts depend on them); only their levels move.
- A seed shows the same world on the map page only while `world_randomizer_zone` matches `web/zones.js`.
