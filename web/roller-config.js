// mod-world-randomizer: defaults for the character roller (index.html). Edit to taste: a new visitor (or "Back to
// default") starts from these. Rules loaded from a code or a rules file replace them.
window.RANDOMIZER_ROLLER_CONFIG = {
  era: 'wrath',            // 'wrath', 'tbc' or 'vanilla'
  hardcore: true,          // true: one roll, no rerolls
  display: 'icons',        // 'icons', 'both' or 'names'

  rules: {
    faction: 'same',       // 'same' (one faction, co-op), 'free' or 'different'
    // At most this many players with the same race / class / spec / profession (0 = any)
    limit: { race: 0, cls: 1, spec: 0, prof: 0 },
    profSlot: false,       // true: two players can't share a profession in the same slot
    compat: 'off',         // 'off', 'player' (each player's two go together) or 'party' (the party covers itself)

    // 'free': pick freely. 'draft': before picking, each player bans this many of each, for everyone
    pick: 'free',
    draft: { race: 1, cls: 1, spec: 1, prof: 1 },

    // Banned for everyone. Races: 1 Human, 2 Orc, 3 Dwarf, 4 Night Elf, 5 Undead, 6 Tauren, 7 Gnome, 8 Troll,
    // 10 Blood Elf, 11 Draenei. Classes: 1 Warrior, 2 Paladin, 3 Hunter, 4 Rogue, 5 Priest, 6 Death Knight, 7 Shaman,
    // 8 Mage, 9 Warlock, 11 Druid. Specs: '<class>-<0, 1 or 2>' in talent tab order ('2-2' = Retribution).
    // Professions: 0 Alchemy, 1 Blacksmithing, 2 Enchanting, 3 Engineering, 4 Herbalism, 5 Inscription,
    // 6 Jewelcrafting, 7 Leatherworking, 8 Mining, 9 Skinning, 10 Tailoring. Factions: 'alliance', 'horde'.
    ban: { faction: [], race: [], cls: [], spec: [], prof: [] }
  }
};
