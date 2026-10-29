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
- **Quests** of a moved zone move with it (`Randomizer.Quests.Shift`): their level, so the XP and the colour in the
  quest log follow, their level requirement and their reward money. Elwynn Forest in 41-50 has level 41-50 quests
  that can be taken from level 41 (`Randomizer.Quests.MinLevelFromBracket`: the bottom of the zone's bracket).
  `.randomizer quests` lists the levels before and after. Class, profession and holiday quests keep their levels. This is the same for every
  player, so it changes the quests in memory at startup rather than through the core's quest level hook (that hook
  is per player, which mod-dynamic-scaling needs and this module doesn't).
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

- **Talents earlier** (optional): `Randomizer.Talents.StartLevel` gives the first talent point before level 10
  (1 to 10). By default the usual total at max level is spread from there (71 at 80); the addon opens the talent
  frame from that level (from level 1 when achievements gave bonus talent points).
- **Where you start**: `Randomizer.PlayerStart.PreferLowZones` picks a bracket 1 zone that was already low
  level before the shuffle when there is one; `.coop create <zone id>` starts the party (or just you) in the zone
  you choose, such as the one the roller page shows.

- **Co-op**: `.coop create` gives a 6 letter code; up to `Randomizer.Coop.MaxSize` friends (5 by default) type
  `.coop join <code>` and start in the same place, in the leader's group.

- **Achievements**: 144 account-wide achievements (levels, zones, quests, gold, HP, deaths, rares, weapon skills,
  professions, specializations...) whose rewards count on every character of the account. See below.
  Kills count for the whole group nearby (`Randomizer.Achievements.GroupCredit`), not only the killing blow.

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

- `index.html`: character roller, for one player or a party (up to 40, two per row). Each player rolls a race
  (shown as its face and gender), class, spec and two professions, and can reroll everything or one of them, plus
  the faction and the start zone; the world seed can be rerolled too. Rerolls are counted per player and per thing.
  Each player has their own preferences: what may be rolled (click the icons; Death Knights are off by default,
  they start at 55), and how: Random, Chances (a weight per race, class, spec...), Shuffle (each comes up once
  before any repeats) or Pick. Professions can be made to include a gathering and/or a crafting one.
  Party rules: one faction or each their own, what may be the same for two players (race, class, spec,
  professions), and compatible professions (each player's two go together, or the party covers itself: Mining +
  Alchemy next to Herbalism + Jewelcrafting). Players can trade races, classes, specs (same class) and
  professions. Each player's code carries their roll, preferences and counts; several codes combine into one
  party, and the party code carries everything, the world seed included. Codes are packed short (about 60
  characters for one player, 100 for a party of five) so they fit in the game's chat; older codes still load and
  "Paste an old code" converts them. Hardcore mode locks the first roll.
  Icons show their name on hover; "Show" switches to names.
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
- `world_randomizer_hardcore_taint`: characters ruled out of going hardcore (a trade, a mail, reaching level 2, a
  failed check), saved for good with the first reason; `allowed = 1` is an admin override (`.hardcore allow`).
- `world_randomizer_coop` / `world_randomizer_coop_member`: co-op parties, their start point, and who joined.
- `world_randomizer_ach_done`, `_counter`, `_zone`, `_run`, `_rare`, `_choice`, `_skill`: achievement progress per
  account (and per character for a run), the chosen starting profession, saved off-class skill values.

`tools/generate_from_aowow.py <aowow>/Stuff/zone-extract` rebuilds the zone SQL and `web/zones.js` after a new
aowow setup.

## Commands

| Command | Who | What |
|---|---|---|
| `.randomizer info` | players | Seed, stats era, and your zone's old and new bracket |
| `.randomizer zones` | GM | Every zone and its bracket |
| `.randomizer reroll [seed]` | admin | Store a new seed; the world changes after a restart |
| `.randomizer start` | GM | Teleport to a bracket 1 start point for your faction |
| `.randomizer quests [id]` | GM | Quest levels now and before the shift: one quest, the selected NPC's, or your zone's (green: you can take it) |
| `.rach` | players | Your account's achievement rewards |
| `.rach list [category]` | players | Achievements, done or not |
| `.rach profession [name]` | players | Choose the starting profession (once unlocked) |
| `.coop` | players | Your co-op party and its code |
| `.coop create` | players | Make a party and get its code (up to `Randomizer.Coop.MaxLevel`) |
| `.coop join <code>` | players | Join a friend's party and start where they start |
| `.hardcore` | players | Your hardcore status |
| `.hardcore on confirm` | players | Go hardcore (Mode 1): level 1, starting gear and money only, never mailed or traded |
| `.hardcore fallen` | players | The last 10 hardcore deaths |
| `.hardcore check <name or GUID>` | GM | What is saved about a character's hardcore eligibility |
| `.hardcore allow <name or GUID>` | admin | Override: the character may go hardcore whatever the checks say |
| `.hardcore block <name or GUID>` | admin | Take an override back, or rule a character out by hand |
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

## Achievements

Account-wide, like a roguelite meta progression: a **run** is one character's life, **lifetime** counts every
character on the account. Each achievement can have an icon, a description and several rewards; they live in
`world_randomizer_achievement` and `world_randomizer_achievement_reward` (world DB), and
`tools/generate_achievements.py` builds the shipped list (taken from the reference screenshots).

Reward types (`type` column; numbers in `src/WorldRandomizerAchievements.h`): XP %, level cap (+levels over
`Randomizer.Achievements.BaseLevelCap`), talent points per level, flat talent points, start gold, start items,
looted money %, health / mana regen %, skill rate (per skill or every profession), armor / weapon type usable by
any class, learn a spell (optionally once a profession is known), extra profession slots, start with a profession,
unlock a starting profession choice (`.rach profession`), + starting skill, gathering yield multiplier, a spell at
a level (the level 60 riding), and titles (the Explorer, Loremaster, the Seeker, the Patient). Type 0 is text
only, for rewards that need features the module doesn't have yet (the treasure zone, enchanted drops,
poisons for any class); the two treasure zone achievements are shipped disabled.

"Usable by any class" gives the character the armor / weapon skill and proficiency on every login: the core
deletes skills a race / class may not have (SkillRaceClassInfo.dbc) when it loads a character, so the module saves
their values at logout and puts them back. Start items and gold from achievements count as starting gear for the
hardcore fresh-start check.

Each achievement also has an `expansion` mask (1 Vanilla, 2 TBC, 4 Wrath, added up; 0 = every expansion) and a
`parent_id`: versions of the same achievement share a parent (0 or the row's own id is the base). Only the versions
for `Randomizer.Achievements.Expansion` count (by default it follows the stats era); the addon shows the content
of each version and lists the others when you hover it.

The shipped list is the original (Vanilla) creator's. Its Vanilla rows keep their ids; TBC versions are id + 2000 and
Wrath versions id + 4000. What changes per expansion:

| Achievements | Vanilla | TBC | Wrath |
|---|---|---|---|
| Levels | 10-60, XP +50% | 10-70, XP +65% | 10-80, XP +100% |
| Gold in a run | 10 / 50 / 250 / 1000, +1g start | 25 / 100 / 500 / 2000, +2g | 50 / 250 / 1000 / 5000, +2g |
| Max health | 500 / 1000 / 2000 / 5000 | 1000 / 2500 / 5000 / 10000 | 2000 / 5000 / 10000 / 25000 |
| Weapon skills - Max | 300 | 350 | 400 |
| Professions 375 / 450 | - | 375 (with Jewelcrafting) | 375 and 450 (with Inscription) |
| Discover all zones | epic mount at 60 | flying at 70 | epic mount at 40 |

Requirements follow each expansion's numbers, and levelling rewards grow by 1 / XP rate (the default rates 0.75 and
0.5), because each level takes longer there. Everything else (zones, quests, rares, deaths, gathering, crafting,
weapon skill 150 / 225, professions up to 300) is the same in every expansion. Change the numbers in
`tools/generate_achievements.py` and run it, or edit the rows directly.

Commands: `.rach` (your account's rewards), `.rach list [category]`, `.rach profession [name]`.

### The addon

`addon/RandomizerAchievements` is a WoW 3.3.5 addon: copy the folder into the client's `Interface\AddOns`.
`/randach` opens a window with the categories, each achievement's icon, rewards, description and progress bar, and
buttons for each party / raid member to see their account's achievements and this run's stats. It talks to the
server with addon whispers to yourself (prefix `RANDACH`, protocol in `src/WorldRandomizerAchievements.h`), so
nothing is shown in chat. `/randach <name>` opens a group member directly. If the server doesn't answer within
10 seconds of logging in (no module, `Randomizer.Enable = 0` or achievements off), the addon turns itself off until
the next login.

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

## Drop / remove / uninstall

`data/sql/drop_remove_uninstall/` has a `world.sql` and a `characters.sql` that take the module back out: they drop its
tables, remove its custom spells (and any saved auras of them) and its rows in `updates`, so a later reinstall starts
clean. Replaced creatures, levels, player base stats, XP rates and start locations are only changed in memory (the
core's `player_class_stats` / `player_race_stats` are never edited), so they are the core's again once the module is
gone. Rewards already given (spells, titles, items, gold) stay on the characters. Soft deleted hardcore characters stay
core deleted characters (`.character deleted restore`); `characters.sql` has a block, commented out, that gives them all
back to their accounts.

1. Take the module out of the build (move it out of `modules/`, or add it to `-DDISABLED_AC_MODULES`) and rebuild.
   With the module still built, the DB updater installs everything again on the next start.
2. Run `world.sql` on the world database and `characters.sql` on the characters database.

## Known limits

- Shifted quests are changed in memory: a core `.reload quest_template` puts the original levels back until the
  next restart. After a new seed, players should delete the client's `Cache` folder, or the quest log may show the
  old levels until the client asks the server again.
- Quests count by their zone (`ZoneOrSort`): a quest given in one zone but sorted under another moves with the
  zone it is sorted under; dungeon quests keep their levels.
- Replaced creatures keep the spawn's waypoints, mounts and auras from `creature_addon`.
- Dungeon trash and bosses keep their entries (boss scripts depend on them); only their levels move.
- Without mod-dynamic-scaling, dungeon levels are picked when creatures spawn, from the players inside at that
  moment; someone joining later doesn't move creatures that are already up. The collective rescales them.
- A seed shows the same world on the map page only while `world_randomizer_zone` matches `web/zones.js`.
