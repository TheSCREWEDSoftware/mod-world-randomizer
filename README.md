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
- **Dungeons** follow the bracket their entrance zone landed in. By default (`Randomizer.Dungeons.Mode = 2`) their
  creatures scale to the group's level but stay inside that bracket: Ragefire Chasm in a 31-40 bracket scales
  between 31 and 40, bosses still above their trash. This works on its own; with mod-dynamic-scaling installed and
  the collective on, that module does it instead and adds its Simple mode buff.
- **New characters** start at a graveyard in a bracket 1 zone of their own faction (then contested zones), with
  the hearthstone set there. Death Knights keep Ebon Hold.
- **Player stats era**: base stats per level from Wrath (AzerothCore), TBC (cMaNGOS-TBC) or Vanilla
  (vMaNGOS), with an XP rate per era (Wrath x0.5, TBC x0.75, Vanilla x1 by default).

- **Hardcore** (optional): one life, chosen by the player (`.hardcore on confirm`) or forced for every new
  character. Choosing it is only for a fresh character: level 1, nothing but its starting gear and starting money,
  and no mail received or trade made, ever. A dead hardcore character is never deleted for good: it is soft deleted with the core's own unlink
  (or locked, or left a ghost), and `world_randomizer_hardcore` keeps it with its owner account, level, killer and
  date. A bugged death is undone with `.hardcore restore <name or GUID> [reason]`.

- **Co-op**: `.coop create` gives a 6 letter code; up to `Randomizer.Coop.MaxSize` friends (5 by default) type
  `.coop join <code>` and start in the same place, in the leader's group.

Every feature has its own switch; `Randomizer.Enable` is the master switch and `Randomizer.Announce` the login
message. Nothing is written to `creature`: the swaps happen in memory at startup. `Randomizer.World.Enable = 0`
and a restart bring the normal world back.

## How new characters start

1. A character is created as usual, at its race's normal start.
2. On its very first login the module picks a random graveyard in a bracket 1 zone: first zones of the
   character's own faction, then contested zones (only graveyards that faction may use), teleports the character
   there and sets the hearthstone there. Death Knights keep Ebon Hold.
3. `Randomizer.Brackets.FactionStarts` (on by default) makes sure each faction has one of its own zones in bracket 1,
   so players don't all land in contested zones (without it both factions get one in about 1 seed out of 8).
4. Playing together: one player types `.coop create` right away (level 1) and shares the code; the others type
   `.coop join <code>` and are moved to the leader's start, hearthstone included. The seed is kept in the characters DB, so the world stays the same
across restarts until you reroll it.

## Web pages (`web/`)

Plain HTML + JavaScript, no build step.

- `index.html`: character roller. A co-op panel rolls a whole party (party size, no duplicate classes) and gives
  a share code carrying the rolls, era, allowed lists and world seed; friends paste it to see the same party.
  The rest: Era, faction, race, class, spec, two professions; tick which factions, races and
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
- `world_randomizer_instance`: entrance zone per instance where the area trigger lookup can't find one (city
  entrances: Ragefire Chasm, the Stockade, the Violet Hold).
- `world_randomizer_dungeon_bracket`: written at startup for mod-dynamic-scaling (collective).
- `world_randomizer_spawn_zone`: cache. Most `creature` rows have `zoneId = 0`; the first start works their
  zones out from the terrain (slow once) and keeps them here.
- `world_randomizer_levelstats` / `world_randomizer_classlevelstats`: era 1 Vanilla (vMaNGOS world database
  `world_full_14_june_2021`) and era 2 TBC (cMaNGOS `mangos-tbc` `sql/base/mangos.sql`).

Characters DB:

- `world_randomizer_state`: the world seed.
- `world_randomizer_hardcore`: every hardcore character, owner account, status, level, killer, death date.
- `world_randomizer_hardcore_revive`: every restore, with the GM and the reason.
- `world_randomizer_hardcore_taint`: characters that received mail or opened a trade, so they can't go hardcore.
- `world_randomizer_coop` / `world_randomizer_coop_member`: co-op parties, their start point, and who joined.

`tools/generate_from_aowow.py <aowow>/Stuff/zone-extract` rebuilds the zone SQL and `web/zones.js` after a new
aowow setup.

## Commands

| Command | Who | What |
|---|---|---|
| `.randomizer info` | players | Seed, stats era, and your zone's old and new bracket |
| `.randomizer zones` | GM | Every zone and its bracket |
| `.randomizer reroll [seed]` | admin | Store a new seed; the world changes after a restart |
| `.randomizer start` | GM | Teleport to a bracket 1 start point for your faction |
| `.coop` | players | Your co-op party and its code |
| `.coop create` | players | Make a party and get its code (up to `Randomizer.Coop.MaxLevel`) |
| `.coop join <code>` | players | Join a friend's party and start where they start |
| `.hardcore` | players | Your hardcore status |
| `.hardcore on confirm` | players | Go hardcore (Mode 1): level 1, starting gear and money only, never mailed or traded |
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

## With mod-dynamic-scaling (collective)

With both modules enabled and both collective switches on (`Randomizer.Collective.DynamicScaling` here,
`DynamicScaling.Collective.WorldRandomizer` there, both on by default), each reads the other's settings from the
config and they split the work: this module writes every dungeon's bracket (from its entrance zone) to
`world_randomizer_dungeon_bracket`, and mod-dynamic-scaling scales those dungeons in Simple mode inside that
bracket (Ragefire Chasm in a 31-40 bracket scales between 31 and 40) and leaves the open world alone. Dungeons
with a city entrance follow the zone around the city (`world_randomizer_instance`: Ragefire Chasm follows
Durotar, the Stockade Elwynn Forest, the Violet Hold Crystalsong Forest).

Turn either switch off and they ignore each other; then don't run both on the same creatures (turn off
`DynamicScaling.Instance.LevelScaling.Enable` and the open world zone rows there).

## Known limits

- Quests keep their own levels and rewards; only their kill targets move levels.
- Replaced creatures keep the spawn's waypoints, mounts and auras from `creature_addon`.
- Dungeon trash and bosses keep their entries (boss scripts depend on them); only their levels move.
- Without mod-dynamic-scaling, dungeon levels are picked when creatures spawn, from the players inside at that
  moment; someone joining later doesn't move creatures that are already up. The collective rescales them.
- A seed shows the same world on the map page only while `world_randomizer_zone` matches `web/zones.js`.
