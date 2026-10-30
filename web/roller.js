// mod-world-randomizer: the character roller (index.html). Solo is a party of one.
// Each player has their own preferences (what may be rolled, and how: random, chances, shuffle or a fixed pick),
// their roll, and their reroll counts. Codes carry all of it, so several players' codes combine into one party.
(function () {
  'use strict';

  var R = window.WorldRandomizer;

  // ---- Game data (3.3.5) ----

  var RACES = {
    1: { name: 'Human', faction: 'alliance' },
    2: { name: 'Orc', faction: 'horde' },
    3: { name: 'Dwarf', faction: 'alliance' },
    4: { name: 'Night Elf', faction: 'alliance' },
    5: { name: 'Undead', faction: 'horde' },
    6: { name: 'Tauren', faction: 'horde' },
    7: { name: 'Gnome', faction: 'alliance' },
    8: { name: 'Troll', faction: 'horde' },
    10: { name: 'Blood Elf', faction: 'horde', since: 'tbc' },
    11: { name: 'Draenei', faction: 'alliance', since: 'tbc' }
  };

  var CLASSES = {
    1: { name: 'Warrior', specs: ['Arms', 'Fury', 'Protection'] },
    2: { name: 'Paladin', specs: ['Holy', 'Protection', 'Retribution'] },
    3: { name: 'Hunter', specs: ['Beast Mastery', 'Marksmanship', 'Survival'] },
    4: { name: 'Rogue', specs: ['Assassination', 'Combat', 'Subtlety'] },
    5: { name: 'Priest', specs: ['Discipline', 'Holy', 'Shadow'] },
    6: { name: 'Death Knight', specs: ['Blood', 'Frost', 'Unholy'], since: 'wrath' },
    7: { name: 'Shaman', specs: ['Elemental', 'Enhancement', 'Restoration'] },
    8: { name: 'Mage', specs: ['Arcane', 'Fire', 'Frost'] },
    9: { name: 'Warlock', specs: ['Affliction', 'Demonology', 'Destruction'] },
    11: { name: 'Druid', specs: ['Balance', 'Feral', 'Restoration'] }
  };

  // Race -> classes in 3.3.5. Vanilla and TBC are the same minus Death Knights (and minus the TBC races).
  var COMBOS = {
    1: [1, 2, 4, 5, 6, 8, 9],
    2: [1, 3, 4, 6, 7, 9],
    3: [1, 2, 3, 4, 5, 6],
    4: [1, 3, 4, 5, 6, 11],
    5: [1, 4, 5, 6, 8, 9],
    6: [1, 3, 6, 7, 11],
    7: [1, 4, 6, 8, 9],
    8: [1, 3, 4, 5, 6, 7, 8],
    10: [2, 3, 4, 5, 6, 8, 9],
    11: [1, 2, 3, 5, 6, 7, 8]
  };

  // gather: a gathering profession; needs: the gathering profession it lives on
  var PROFESSIONS = [
    { name: 'Alchemy', needs: 'Herbalism' },
    { name: 'Blacksmithing', needs: 'Mining' },
    { name: 'Enchanting' },
    { name: 'Engineering', needs: 'Mining' },
    { name: 'Herbalism', gather: true },
    { name: 'Inscription', needs: 'Herbalism', since: 'wrath' },
    { name: 'Jewelcrafting', needs: 'Mining', since: 'tbc' },
    { name: 'Leatherworking', needs: 'Skinning' },
    { name: 'Mining', gather: true },
    { name: 'Skinning', gather: true },
    { name: 'Tailoring' }
  ];

  // What each spec does in a group
  var SPEC_ROLES = {
    1: ['Melee DPS', 'Melee DPS', 'Tank'], 2: ['Healer', 'Tank', 'Melee DPS'],
    3: ['Ranged DPS', 'Ranged DPS', 'Ranged DPS'], 4: ['Melee DPS', 'Melee DPS', 'Melee DPS'],
    5: ['Healer', 'Healer', 'Ranged DPS'], 6: ['Tank', 'Tank / Melee DPS', 'Melee DPS'],
    7: ['Ranged DPS', 'Melee DPS', 'Healer'], 8: ['Ranged DPS', 'Ranged DPS', 'Ranged DPS'],
    9: ['Ranged DPS', 'Ranged DPS', 'Ranged DPS'], 11: ['Ranged DPS', 'Melee DPS / Tank', 'Healer']
  };

  // A few racials of each race (Wrath), with their icons (img/racial)
  var RACIALS = {
    1: [['Every Man for Himself', 'spell_shadow_charm', 'Breaks out of stuns and fear'],
      ['Diplomacy', 'inv_misc_note_02', '10% more reputation'],
      ['The Human Spirit', 'inv_enchant_shardbrilliantsmall', '3% more Spirit']],
    2: [['Blood Fury', 'racial_orc_berserkerstrength', 'Attack power and spell power for 15 sec'],
      ['Hardiness', 'inv_helmet_23', 'Stuns last 15% shorter'],
      ['Axe Specialization', 'inv_axe_02', 'Expertise with axes and fist weapons']],
    3: [['Stoneform', 'spell_shadow_unholystrength', 'Removes poison, disease and bleeding; 10% armor'],
      ['Gun Specialization', 'inv_musket_03', '1% more crit with guns'],
      ['Frost Resistance', 'spell_frost_wizardmark', '+10 Frost resistance']],
    4: [['Shadowmeld', 'ability_ambush', 'Vanish while standing still'],
      ['Nature Resistance', 'spell_nature_spiritarmor', '+10 Nature resistance'],
      ['Wisp Spirit', 'spell_nature_wispsplode', 'A fast wisp when dead']],
    5: [['Will of the Forsaken', 'spell_shadow_raisedead', 'Breaks charm, fear and sleep'],
      ['Cannibalize', 'ability_racial_cannibalize', 'Eat corpses to heal'],
      ['Underwater Breathing', 'spell_shadow_demonbreath', 'Breathe under water much longer']],
    6: [['War Stomp', 'ability_warstomp', 'Stuns nearby enemies'],
      ['Endurance', 'spell_nature_unyeildingstamina', '5% more base health'],
      ['Cultivation', 'inv_misc_flower_01', 'Herbalism +15, gathers faster']],
    7: [['Escape Artist', 'ability_rogue_trip', 'Breaks out of roots and snares'],
      ['Expansive Mind', 'inv_enchant_essenceeternallarge', '5% more Intellect'],
      ['Engineering Specialization', 'inv_misc_gear_01', 'Engineering +15']],
    8: [['Berserking', 'racial_troll_berserk', '20% faster attacks and casting'],
      ['Regeneration', 'spell_nature_regenerate', 'Health regenerates in combat'],
      ['Beast Slaying', 'inv_misc_pelt_bear_ruin_02', '5% more damage to beasts']],
    10: [['Arcane Torrent', 'spell_shadow_teleport', 'Silences nearby enemies, restores power'],
      ['Magic Resistance', 'spell_shadow_antimagicshell', '+5 resistance to every school'],
      ['Arcane Affinity', 'spell_holy_magicalsentry', 'Enchanting +10']],
    11: [['Gift of the Naaru', 'spell_holy_holyprotection', 'Heals over 15 sec'],
      ['Heroic Presence', 'inv_helmet_21', '1% hit for the party'],
      ['Shadow Resistance', 'spell_shadow_antishadow', '+10 Shadow resistance']]
  };

  // Two professions that go together on one character: a gatherer and what it feeds, and Tailoring + Enchanting
  function professionsGoTogether(a, b) {
    var pa = PROFESSIONS[a];
    var pb = PROFESSIONS[b];
    if (pa.needs === pb.name || pb.needs === pa.name) return true;
    var names = [pa.name, pb.name].sort().join('+');
    return names === 'Enchanting+Tailoring';
  }

  var ERAS = ['vanilla', 'tbc', 'wrath'];
  var ERA_ORDER = { vanilla: 0, tbc: 1, wrath: 2 };
  var ERA_NAME = { tbc: 'TBC and later', wrath: 'Wrath only' };
  var DEFAULT_BRACKETS = '1-10,11-20,21-30,31-40,41-50,51-60,61-70,71-80';
  var DK = '6';

  var CATS = ['faction', 'race', 'class', 'spec', 'prof1', 'prof2'];
  var CAT_LABEL = {
    faction: 'Faction', race: 'Race', 'class': 'Class', spec: 'Spec', prof1: 'Profession 1', prof2: 'Profession 2'
  };
  var MODES = ['random', 'chances', 'shuffle', 'pick'];
  var MODE_LABEL = { random: 'Random', chances: 'Chances', shuffle: 'Shuffle', pick: 'Pick' };
  var MODE_TIP = {
    random: 'Every ticked one has the same chance',
    chances: 'Set how likely each one is',
    shuffle: 'Like a deck of cards: each ticked one comes up once before any repeats',
    pick: 'Always the one you click'
  };
  var COUNT_KEYS = ['all', 'faction', 'race', 'class', 'spec', 'prof1', 'prof2', 'zone', 'world'];
  var COUNT_LABEL = {
    all: 'everything', faction: 'faction', race: 'race', 'class': 'class', spec: 'spec', prof1: 'profession 1',
    prof2: 'profession 2', zone: 'start zone', world: 'world seed'
  };
  var PROF_RULES = ['any', 'gather', 'craft', 'both'];

  var KEYS = {
    faction: ['alliance', 'horde'],
    race: Object.keys(RACES),
    'class': Object.keys(CLASSES),
    spec: [],
    prof1: PROFESSIONS.map(function (p, i) { return String(i); }),
    prof2: PROFESSIONS.map(function (p, i) { return String(i); })
  };
  KEYS['class'].forEach(function (c) { [0, 1, 2].forEach(function (i) { KEYS.spec.push(c + '-' + i); }); });
  // Ban lists: rules key, KEYS list
  var DRAFT_KEYS = { race: KEYS.race, cls: KEYS['class'], spec: KEYS.spec, prof: KEYS.prof1 };
  var BAN_CATS = [['faction', 'faction'], ['race', 'race'], ['cls', 'class'], ['spec', 'spec'], ['prof', 'prof1']];

  function since(cat, key) {
    if (cat === 'race') return RACES[key].since;
    if (cat === 'class') return CLASSES[key].since;
    if (cat === 'spec') return CLASSES[key.split('-')[0]].since;
    if (cat === 'prof1' || cat === 'prof2') return PROFESSIONS[key].since;
    return undefined;
  }

  function inEra(cat, key, era) {
    var from = since(cat, key);
    return !from || ERA_ORDER[era] >= ERA_ORDER[from];
  }

  function itemName(cat, key) {
    if (cat === 'faction') return key === 'alliance' ? 'Alliance' : 'Horde';
    if (cat === 'race') return RACES[key].name;
    if (cat === 'class') return CLASSES[key].name;
    if (cat === 'spec') {
      var parts = key.split('-');
      return CLASSES[parts[0]].specs[parts[1]] + ' ' + CLASSES[parts[0]].name;
    }
    return PROFESSIONS[key].name;
  }

  function itemIcon(cat, key, gender) {
    if (cat === 'faction') return 'img/faction/' + key + '.jpg';
    if (cat === 'race') return 'img/race/' + key + (gender || 'm') + '.webp';
    if (cat === 'class') return 'img/class/' + key + '.webp';
    if (cat === 'spec') return 'img/spec/' + key + '.jpg';
    return 'img/prof/' + key + '.jpg';
  }

  function esc(text) {
    return String(text).replace(/[&<>"']/g, function (c) {
      return { '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;', "'": '&#39;' }[c];
    });
  }

  function rand(count) {
    return new R.Rng(R.randomSeed()).below(count);
  }

  function pickOne(list) {
    return list[rand(list.length)];
  }

  // ---- State ----

  function defaultCategory(cat) {
    var s = { mode: 'random', allow: {}, weight: {}, pick: '' };
    KEYS[cat].forEach(function (key) {
      s.allow[key] = !(cat === 'class' && key === DK); // Death Knights start at 55 in Ebon Hold: off by default
      s.weight[key] = 10;
    });
    return s;
  }

  function defaultSettings() {
    var settings = { gender: 'random', rollSpec: true, rollProfs: true, profRule: 'any', cats: {} };
    CATS.forEach(function (cat) { settings.cats[cat] = defaultCategory(cat); });
    return settings;
  }

  // A player's draft bans (rules.pick 'draft'): keys they banned for everyone
  function emptyDraft() {
    return { race: [], cls: [], spec: [], prof: [] };
  }

  function zeroCounts() {
    var counts = { trade: 0 };
    COUNT_KEYS.forEach(function (key) { counts[key] = 0; });
    return counts;
  }

  function newPlayer(number) {
    return { name: 'Player ' + number, settings: defaultSettings(), roll: null, counts: zeroCounts(), decks: {},
      draft: emptyDraft() };
  }

  // Rules over every player: the faction rule, how many players may share a race / class / spec / profession
  // (0 = any), professions per slot, compatible professions, and what is banned outright
  var CONFIG = window.RANDOMIZER_ROLLER_CONFIG || {};
  var DRAFT_CATS = ['race', 'cls', 'spec', 'prof'];

  // The rules everyone starts from: roller-config.js, or these
  function defaultRules() {
    var rules = {
      faction: 'same', limit: { race: 0, cls: 1, spec: 0, prof: 0 }, profSlot: false, compat: 'off',
      pick: 'free', draft: { race: 1, cls: 1, spec: 1, prof: 1 },
      ban: { faction: {}, race: {}, cls: {}, spec: {}, prof: {} }
    };
    var c = CONFIG.rules;
    if (!c) return rules;
    if (['same', 'free', 'different'].indexOf(c.faction) >= 0) rules.faction = c.faction;
    if (['off', 'player', 'party'].indexOf(c.compat) >= 0) rules.compat = c.compat;
    if (['free', 'draft'].indexOf(c.pick) >= 0) rules.pick = c.pick;
    rules.profSlot = !!c.profSlot;
    DRAFT_CATS.forEach(function (k) {
      if (c.limit && c.limit[k] !== undefined) rules.limit[k] = Math.max(0, Math.min(40, Number(c.limit[k]) || 0));
      if (c.draft && c.draft[k] !== undefined) rules.draft[k] = Math.max(0, Math.min(15, Number(c.draft[k]) || 0));
    });
    Object.keys(rules.ban).forEach(function (k) {
      ((c.ban && c.ban[k]) || []).forEach(function (key) { rules.ban[k][String(key)] = true; });
    });
    return rules;
  }

  // Rules saved by earlier versions of the page (duplicate switches instead of limits)
  function normalizeRules(r) {
    var rules = defaultRules();
    if (!r) return rules;
    rules.faction = r.faction || 'same';
    rules.compat = r.compat || 'off';
    if (r.limit) {
      Object.keys(rules.limit).forEach(function (k) { rules.limit[k] = Math.max(0, Number(r.limit[k]) || 0); });
    } else {
      rules.limit.race = r.dupRace === false ? 1 : 0;
      rules.limit.cls = r.dupClass === true ? 0 : 1;
      rules.limit.spec = r.dupSpec === false ? 1 : 0;
      rules.limit.prof = r.profDup === 'no' ? 1 : 0;
    }
    rules.profSlot = r.profSlot !== undefined ? !!r.profSlot : r.profDup === 'slot';
    if (r.pick === 'free' || r.pick === 'draft') rules.pick = r.pick;
    if (r.draft) DRAFT_CATS.forEach(function (k) { rules.draft[k] = Math.max(0, Number(r.draft[k]) || 0); });
    if (r.ban) Object.keys(rules.ban).forEach(function (k) { rules.ban[k] = Object.assign({}, r.ban[k] || {}); });
    return rules;
  }

  function defaultState() {
    return {
      v: 3, era: ERAS.indexOf(CONFIG.era) >= 0 ? CONFIG.era : 'wrath',
      hardcore: CONFIG.hardcore === undefined ? true : !!CONFIG.hardcore,
      display: ['icons', 'both', 'names'].indexOf(CONFIG.display) >= 0 ? CONFIG.display : 'icons', tab: 0,
      party: defaultRules(),
      players: [newPlayer(1)]
    };
  }

  var STATE_KEY = 'wr-roller';

  // Fill in anything missing (older saves, codes from older pages)
  function normalizePlayer(p, number) {
    var fresh = newPlayer(number);
    p.name = p.name || fresh.name;
    p.settings = p.settings || fresh.settings;
    CATS.forEach(function (cat) {
      var s = p.settings.cats[cat] = p.settings.cats[cat] || defaultCategory(cat);
      var d = defaultCategory(cat);
      KEYS[cat].forEach(function (key) {
        if (s.allow[key] === undefined) s.allow[key] = d.allow[key];
        if (s.weight[key] === undefined) s.weight[key] = d.weight[key];
      });
      if (MODES.indexOf(s.mode) < 0) s.mode = 'random';
    });
    ['gender', 'rollSpec', 'rollProfs', 'profRule'].forEach(function (key) {
      if (p.settings[key] === undefined) p.settings[key] = fresh.settings[key];
    });
    p.counts = Object.assign(zeroCounts(), p.counts || {});
    p.decks = p.decks || {};
    p.roll = p.roll || null;
    var draft = emptyDraft();
    DRAFT_CATS.forEach(function (k) { draft[k] = ((p.draft && p.draft[k]) || []).map(String); });
    p.draft = draft;
    return p;
  }

  function loadState() {
    var state = null;
    try { state = JSON.parse(localStorage.getItem(STATE_KEY) || 'null'); } catch (e) { state = null; }
    if (!state || state.v !== 3 || !Array.isArray(state.players) || !state.players.length) {
      state = defaultState();
      // The allowed lists of the previous page
      try {
        var old = JSON.parse(localStorage.getItem('wr-allowed') || 'null');
        if (old) {
          var cats = state.players[0].settings.cats;
          Object.keys(old.factions || {}).forEach(function (k) { cats.faction.allow[k] = !!old.factions[k]; });
          Object.keys(old.races || {}).forEach(function (k) { cats.race.allow[k] = !!old.races[k]; });
          Object.keys(old.classes || {}).forEach(function (k) { cats['class'].allow[k] = !!old.classes[k]; });
        }
      } catch (e) { /* storage blocked */ }
    }
    state.party = normalizeRules(state.party);
    state.players.forEach(function (p, i) { normalizePlayer(p, i + 1); });
    state.players.forEach(function (p, i) { if (/^Player \d+$/.test(p.name)) p.name = 'Player ' + (i + 1); });
    if (ERAS.indexOf(state.era) < 0) state.era = 'wrath';
    if (!(state.tab < state.players.length)) state.tab = 0;
    return state;
  }

  var state = loadState();

  function save() {
    try { localStorage.setItem(STATE_KEY, JSON.stringify(state)); } catch (e) { /* storage blocked */ }
  }

  // Players still called "Player N" follow their place in the party, so names never repeat; renamed ones keep theirs
  function renumberPlayers() {
    state.players.forEach(function (p, i) {
      if (/^Player \d+$/.test(p.name)) p.name = 'Player ' + (i + 1);
    });
  }

  function locked(p) {
    return state.hardcore && !!p.roll;
  }

  // ---- Drawing one thing ----

  // Banned by the rules (or its faction / class is)
  function banned(cat, key) {
    var ban = state.party.ban;
    if (cat === 'faction') return !!ban.faction[key];
    if (cat === 'race') return !!ban.race[key] || !!ban.faction[RACES[key].faction] || !!draftedBy('race', key);
    if (cat === 'class') return !!ban.cls[key] || !!draftedBy('cls', key);
    if (cat === 'spec') {
      var cls = key.split('-')[0];
      return !!ban.spec[key] || !!ban.cls[cls] || !!draftedBy('spec', key) || !!draftedBy('cls', cls);
    }
    return !!ban.prof[key] || !!draftedBy('prof', key);
  }

  // The player who banned it in the draft, or null
  function draftedBy(dcat, key) {
    if (state.party.pick !== 'draft') return null;
    for (var i = 0; i < state.players.length; i++) {
      if (state.players[i].draft[dcat].indexOf(String(key)) >= 0) return state.players[i];
    }
    return null;
  }

  function isAllowed(p, cat, key) {
    if (banned(cat, key)) return false;
    var s = p.settings.cats[cat];
    if (s.mode === 'pick') return s.pick === key;
    if (!s.allow[key]) return false;
    return s.mode !== 'chances' || s.weight[key] > 0;
  }

  // Everything this player could ever get in this category (the shuffle deck refills with these)
  function allowedKeys(p, cat) {
    return KEYS[cat].filter(function (key) { return inEra(cat, key, state.era) && isAllowed(p, cat, key); });
  }

  function deckOf(p, cat) {
    var deck = (p.decks[cat] || []).filter(function (key) { return isAllowed(p, cat, key); });
    return deck.length ? deck : allowedKeys(p, cat);
  }

  // Draw one key of candidates (already filtered by the rules) the way the player chose; null if none
  function draw(p, cat, candidates) {
    if (!candidates.length) return null;
    var s = p.settings.cats[cat];
    if (s.mode === 'chances') {
      var total = 0;
      candidates.forEach(function (key) { total += Math.max(0, Number(s.weight[key]) || 0); });
      if (total <= 0) return pickOne(candidates);
      var roll = (R.randomSeed() / 4294967296) * total;
      for (var i = 0; i < candidates.length; i++) {
        roll -= Math.max(0, Number(s.weight[candidates[i]]) || 0);
        if (roll < 0) return candidates[i];
      }
      return candidates[candidates.length - 1];
    }
    if (s.mode === 'shuffle') {
      var deck = deckOf(p, cat);
      var left = candidates.filter(function (key) { return deck.indexOf(key) >= 0; });
      return pickOne(left.length ? left : candidates);
    }
    return pickOne(candidates);
  }

  // A drawn key leaves the shuffle deck (once the whole roll is accepted)
  function commit(p, picks) {
    picks.forEach(function (pick) {
      if (pick.key === null || p.settings.cats[pick.cat].mode !== 'shuffle') return;
      var deck = deckOf(p, pick.cat);
      if (deck.indexOf(pick.key) < 0) deck = allowedKeys(p, pick.cat);
      p.decks[pick.cat] = deck.filter(function (key) { return key !== pick.key; });
    });
  }

  // Prefer something new on a reroll, when there is anything else
  function others(candidates, current) {
    var rest = candidates.filter(function (key) { return key !== current; });
    return rest.length ? rest : candidates;
  }

  // ---- What the rest of the party rules out ----

  function blankContext() {
    return { faction: null, notFaction: {}, race: {}, cls: {}, spec: {}, prof1: {}, prof2: {}, profAny: {} };
  }

  function partyContext(index) {
    var ctx = blankContext();
    if (state.players.length < 2) return ctx;
    var rules = state.party;
    var counts = { race: {}, cls: {}, spec: {}, prof: {} };
    state.players.forEach(function (q, j) {
      if (j === index || !q.roll) return;
      var faction = RACES[q.roll.race].faction;
      if (rules.faction === 'same' && !ctx.faction) ctx.faction = faction;
      if (rules.faction === 'different') ctx.notFaction[faction] = true;
      counts.race[q.roll.race] = (counts.race[q.roll.race] || 0) + 1;
      counts.cls[q.roll.cls] = (counts.cls[q.roll.cls] || 0) + 1;
      if (q.roll.spec >= 0) {
        var specKey = q.roll.cls + '-' + q.roll.spec;
        counts.spec[specKey] = (counts.spec[specKey] || 0) + 1;
      }
      q.roll.profs.forEach(function (prof, slot) {
        if (prof < 0) return;
        if (rules.profSlot) ctx['prof' + (slot + 1)][prof] = true;
        counts.prof[prof] = (counts.prof[prof] || 0) + 1;
      });
    });
    // Full: as many players as the limit allows already have it
    [['race', 'race'], ['cls', 'cls'], ['spec', 'spec'], ['prof', 'profAny']].forEach(function (pair) {
      var limit = rules.limit[pair[0]];
      if (!limit) return;
      Object.keys(counts[pair[0]]).forEach(function (key) {
        if (counts[pair[0]][key] >= limit) ctx[pair[1]][key] = true;
      });
    });
    return ctx;
  }

  function specOk(p, cls, i, ctx) {
    var key = cls + '-' + i;
    return isAllowed(p, 'spec', key) && !ctx.spec[key];
  }

  function classOk(p, race, cls, ctx) {
    cls = String(cls);
    return COMBOS[race].indexOf(Number(cls)) >= 0 && inEra('class', cls, state.era) && isAllowed(p, 'class', cls) &&
      !ctx.cls[cls] && (!p.settings.rollSpec || [0, 1, 2].some(function (i) { return specOk(p, cls, i, ctx); }));
  }

  function raceOk(p, race, ctx) {
    race = String(race);
    return inEra('race', race, state.era) && isAllowed(p, 'race', race) && !ctx.race[race] &&
      COMBOS[race].some(function (cls) { return classOk(p, race, cls, ctx); });
  }

  function factionOk(p, faction, ctx) {
    if ((ctx.faction && ctx.faction !== faction) || ctx.notFaction[faction] || !isAllowed(p, 'faction', faction)) {
      return false;
    }
    return KEYS.race.some(function (race) { return RACES[race].faction === faction && raceOk(p, race, ctx); });
  }

  function racesFor(p, faction, ctx) {
    return KEYS.race.filter(function (race) { return RACES[race].faction === faction && raceOk(p, race, ctx); });
  }

  function classesFor(p, race, ctx) {
    return COMBOS[race].map(String).filter(function (cls) { return classOk(p, race, cls, ctx); });
  }

  function specsFor(p, cls, ctx) {
    return [0, 1, 2].filter(function (i) { return specOk(p, cls, i, ctx); })
      .map(function (i) { return cls + '-' + i; });
  }

  function rollGender(p) {
    return p.settings.gender === 'random' ? pickOne(['m', 'f']) : p.settings.gender;
  }

  // ---- Professions ----

  function pairOk(p, a, b) {
    var gathering = (PROFESSIONS[a].gather ? 1 : 0) + (PROFESSIONS[b].gather ? 1 : 0);
    var rule = p.settings.profRule;
    if (rule === 'gather' && gathering < 1) return false;
    if (rule === 'craft' && gathering > 1) return false;
    if (rule === 'both' && gathering !== 1) return false;
    return state.party.compat !== 'player' || professionsGoTogether(a, b);
  }

  // Professions possible in one slot, next to the other slot's (-1 = none yet)
  function profCandidates(p, slot, other, ctx) {
    var cat = 'prof' + slot;
    return KEYS[cat].filter(function (key) {
      var prof = Number(key);
      return inEra(cat, key, state.era) && isAllowed(p, cat, key) && prof !== other && !ctx.profAny[prof] &&
        !ctx[cat][prof] && (other < 0 || pairOk(p, prof, other));
    });
  }

  // Every crafting profession in the party has its gatherer, and every gatherer someone it feeds
  function partyCovered(rolls) {
    var have = {};
    rolls.forEach(function (roll) {
      if (roll) roll.profs.forEach(function (prof) { if (prof >= 0) have[PROFESSIONS[prof].name] = true; });
    });
    return Object.keys(have).every(function (name) {
      var prof = PROFESSIONS.filter(function (x) { return x.name === name; })[0];
      if (prof.needs) return !!have[prof.needs];
      if (prof.gather) {
        return PROFESSIONS.some(function (x) { return x.needs === name && have[x.name]; });
      }
      return true;
    });
  }

  function rollProfPair(p, ctx, picks) {
    var first = profCandidates(p, 1, -1, ctx).filter(function (key) {
      return profCandidates(p, 2, Number(key), ctx).length > 0;
    });
    var a = draw(p, 'prof1', first);
    if (a === null) return null;
    var b = draw(p, 'prof2', profCandidates(p, 2, Number(a), ctx));
    if (b === null) return null;
    picks.push({ cat: 'prof1', key: a }, { cat: 'prof2', key: b });
    return [Number(a), Number(b)];
  }

  // ---- Rolling ----

  var CANT = {
    faction: 'no faction is ticked that has a race and class left',
    race: 'no ticked race has a ticked class (and spec) left',
    'class': 'no ticked class (with a ticked spec) fits',
    spec: 'no ticked spec is left for this class',
    prof: 'no two ticked professions fit the profession rules'
  };

  // A whole character for player p; keep: parts of the current roll to keep (professions, faction)
  function rollCharacter(p, ctx, keep) {
    keep = keep || {};
    var picks = [];
    var faction = ctx.faction || keep.faction;
    if (!faction) {
      faction = draw(p, 'faction', KEYS.faction.filter(function (f) { return factionOk(p, f, ctx); }));
      if (!faction) return { error: CANT.faction };
      picks.push({ cat: 'faction', key: faction });
    } else if (!factionOk(p, faction, ctx)) {
      return { error: 'nothing ticked fits the ' + itemName('faction', faction) };
    }
    var race = draw(p, 'race', racesFor(p, faction, ctx));
    if (!race) return { error: CANT.race };
    var cls = draw(p, 'class', classesFor(p, race, ctx));
    if (!cls) return { error: CANT['class'] };
    picks.push({ cat: 'race', key: race }, { cat: 'class', key: cls });
    var spec = -1;
    if (p.settings.rollSpec) {
      var specKey = draw(p, 'spec', specsFor(p, cls, ctx));
      if (!specKey) return { error: CANT.spec };
      picks.push({ cat: 'spec', key: specKey });
      spec = Number(specKey.split('-')[1]);
    }
    var profs = [-1, -1];
    if (keep.profs) profs = keep.profs.slice();
    else if (p.settings.rollProfs) {
      profs = rollProfPair(p, ctx, picks);
      if (!profs) return { error: CANT.prof };
    }
    return {
      roll: { race: Number(race), cls: Number(cls), gender: rollGender(p), spec: spec, profs: profs, zone: 0 },
      picks: picks
    };
  }

  var TRIES = 400;
  var lastMessage = '';

  function message(text, kind) {
    lastMessage = text ? '<span class="' + (kind || 'bad') + '">' + text + '</span>' : '';
  }

  function needsPartyCover() {
    return state.party.compat === 'party';
  }

  function otherRolls(index) {
    return state.players.map(function (q, j) { return j === index ? null : q.roll; });
  }

  // Roll (or reroll everything for) one player, keeping the rest of the party
  function rollPlayer(index) {
    var p = state.players[index];
    if (locked(p)) return;
    var ctx = partyContext(index);
    var result = null;
    for (var t = 0; t < TRIES; t++) {
      result = rollCharacter(p, ctx);
      if (result.error) break;
      if (!needsPartyCover()) break;
      var rolls = otherRolls(index);
      rolls[index] = result.roll;
      if (partyCovered(rolls)) break;
      result = { error: 'no professions found that cover the party' };
    }
    if (result.error) { message(esc(p.name) + ': ' + result.error + '.'); return; }
    if (p.roll) p.counts.all++;
    commit(p, result.picks);
    p.roll = result.roll;
    message('');
  }

  // Everyone at once (players with a locked hardcore roll keep it)
  function rollEveryone() {
    var players = state.players;
    var keepLocked = players.map(function (p) { return locked(p); });
    var before = players.map(function (p) { return p.roll; });
    var error = '';
    for (var attempt = 0; attempt < 60; attempt++) {
      error = '';
      var picks = players.map(function () { return []; });
      players.forEach(function (p, i) { if (!keepLocked[i]) p.roll = null; });

      // Same faction: one both can live with, drawn with player 1's preferences
      var forced = null;
      if (players.length > 1 && state.party.faction === 'same') {
        var lockedOne = players.filter(function (p, i) { return keepLocked[i]; })[0];
        if (lockedOne) forced = RACES[lockedOne.roll.race].faction;
        else {
          var empty = blankContext();
          var shared = KEYS.faction.filter(function (f) {
            return players.every(function (p) { return factionOk(p, f, empty); });
          });
          forced = draw(players[0], 'faction', shared);
          if (!forced) { error = 'no faction suits every player\'s preferences'; break; }
          picks[0].push({ cat: 'faction', key: forced });
        }
      }

      for (var i = 0; i < players.length && !error; i++) {
        if (keepLocked[i]) continue;
        var ctx = partyContext(i);
        if (forced) ctx.faction = forced;
        // Professions come after everyone's character when the party has to cover itself
        var result = rollCharacter(players[i], ctx, needsPartyCover() ? { profs: [-1, -1] } : null);
        if (result.error) { error = esc(players[i].name) + ': ' + result.error; break; }
        players[i].roll = result.roll;
        picks[i] = picks[i].concat(result.picks);
      }

      if (!error && needsPartyCover()) {
        var covered = false;
        for (var t = 0; t < TRIES && !covered; t++) {
          var profPicks = players.map(function () { return []; });
          players.forEach(function (p, j) { if (!keepLocked[j] && p.settings.rollProfs) p.roll.profs = [-1, -1]; });
          var failed = false;
          players.forEach(function (p, j) {
            if (failed || keepLocked[j] || !p.settings.rollProfs) return;
            var pair = rollProfPair(p, partyContext(j), profPicks[j]);
            if (!pair) failed = true;
            else p.roll.profs = pair;
          });
          if (!failed && partyCovered(players.map(function (p) { return p.roll; }))) {
            covered = true;
            profPicks.forEach(function (list, j) { picks[j] = picks[j].concat(list); });
          }
        }
        if (!covered) error = 'no professions found that cover the party';
      }

      if (!error) {
        players.forEach(function (p, j) {
          if (keepLocked[j]) return;
          if (before[j]) p.counts.all++;
          commit(p, picks[j]);
        });
        message('');
        return;
      }
    }
    players.forEach(function (p, i) { p.roll = before[i]; });
    message(error + '.');
  }

  // One part of a player's roll
  function reroll(index, cat) {
    var p = state.players[index];
    if (!p.roll) { rollPlayer(index); return; }
    if (locked(p)) return;
    var roll = p.roll;
    var ctx = partyContext(index);
    var picks = [];
    var name = esc(p.name) + ': ';
    var current;

    if (cat === 'faction') {
      var faction = RACES[roll.race].faction;
      if (state.players.length > 1 && state.party.faction === 'same') {
        message('Same faction for everyone: the faction can\'t be rerolled ("Roll everyone" rolls a new one).');
        return;
      }
      var factions = KEYS.faction.filter(function (f) {
        return f !== faction && factionOk(p, f, Object.assign({}, ctx, { faction: null }));
      });
      if (!factions.length) { message(name + 'no other faction is possible.'); return; }
      var chosen = draw(p, 'faction', factions);
      var result = rollCharacter(p, Object.assign({}, ctx, { faction: chosen }), { profs: roll.profs });
      if (result.error) { message(name + result.error + '.'); return; }
      result.roll.zone = 0;
      commit(p, [{ cat: 'faction', key: chosen }].concat(result.picks));
      p.roll = result.roll;
    } else if (cat === 'race') {
      current = String(roll.race);
      var races = racesFor(p, RACES[roll.race].faction, ctx).filter(function (race) {
        return classOk(p, race, roll.cls, ctx);
      });
      if (!others(races, current).filter(function (r) { return r !== current; }).length) {
        message(name + 'no other ticked race can be a ' + CLASSES[roll.cls].name + '.');
        return;
      }
      var race = draw(p, 'race', others(races, current));
      picks.push({ cat: 'race', key: race });
      roll.race = Number(race);
      roll.gender = rollGender(p);
    } else if (cat === 'class') {
      current = String(roll.cls);
      var classes = others(classesFor(p, roll.race, ctx), current);
      if (!classes.length || (classes.length === 1 && classes[0] === current)) {
        message(name + 'no other ticked class fits a ' + RACES[roll.race].name + '.');
        return;
      }
      var cls = draw(p, 'class', classes);
      picks.push({ cat: 'class', key: cls });
      roll.cls = Number(cls);
      roll.spec = -1;
      if (p.settings.rollSpec) {
        var newSpec = draw(p, 'spec', specsFor(p, cls, ctx));
        picks.push({ cat: 'spec', key: newSpec });
        roll.spec = Number(newSpec.split('-')[1]);
      }
    } else if (cat === 'spec') {
      current = roll.cls + '-' + roll.spec;
      var specs = others(specsFor(p, String(roll.cls), ctx), current);
      if (!specs.length || (specs.length === 1 && specs[0] === current)) {
        message(name + 'no other ticked ' + CLASSES[roll.cls].name + ' spec.');
        return;
      }
      var spec = draw(p, 'spec', specs);
      picks.push({ cat: 'spec', key: spec });
      roll.spec = Number(spec.split('-')[1]);
    } else if (cat === 'prof1' || cat === 'prof2') {
      var slot = cat === 'prof1' ? 0 : 1;
      current = String(roll.profs[slot]);
      var candidates = profCandidates(p, slot + 1, roll.profs[1 - slot], ctx).filter(function (k) {
        return k !== current;
      });
      var found = null;
      for (var t = 0; t < TRIES && found === null && candidates.length; t++) {
        var key = draw(p, cat, candidates);
        var trial = roll.profs.slice();
        trial[slot] = Number(key);
        var rolls = otherRolls(index);
        rolls[index] = { profs: trial };
        if (!needsPartyCover() || partyCovered(rolls)) found = key;
      }
      if (found === null) {
        message(name + 'no other profession fits ' + (roll.profs[1 - slot] >= 0 ?
          PROFESSIONS[roll.profs[1 - slot]].name + ' and ' : '') + 'the profession rules.');
        return;
      }
      picks.push({ cat: cat, key: found });
      roll.profs[slot] = Number(found);
    } else if (cat === 'zone') {
      var start = startFor(index);
      if (!start || !start.zones.length) { message(name + 'no start zones (pick a world seed first).'); return; }
      var ids = start.zones.map(function (z) { return z.id; });
      if (ids.length < 2) { message(name + 'only one start zone with this seed.'); return; }
      roll.zone = pickOne(ids.filter(function (id) { return id !== roll.zone; }));
    }
    commit(p, picks);
    p.counts[cat]++;
    message('');
  }

  // ---- Start zone and world seed ----

  var zoneCache = { key: '', zones: [] };

  function worldSettings() {
    try { return JSON.parse(localStorage.getItem('wr-map') || '{}') || {}; } catch (e) { return {}; }
  }

  function shuffledWorld() {
    var settings = worldSettings();
    if (!settings.seed) return null;
    var key = JSON.stringify([settings.seed, settings.brackets, settings.scope, settings.balanced, settings.adjacent,
      settings.factionStarts]);
    if (zoneCache.key !== key) {
      var brackets = R.parseBrackets(settings.brackets || DEFAULT_BRACKETS);
      if (!brackets.length) brackets = [{ min: 1, max: 80 }];
      var zones = R.shuffleZones(window.RANDOMIZER_ZONES || [], {
        seed: settings.seed >>> 0, brackets: brackets, scope: settings.scope || 0, balanced: !!settings.balanced,
        adjacent: settings.adjacent === undefined ? true : !!settings.adjacent,
        factionStarts: settings.factionStarts === undefined ? true : !!settings.factionStarts
      });
      var lowest = 0;
      brackets.forEach(function (b, i) { if (b.min < brackets[lowest].min) lowest = i; });
      zoneCache = { key: key, zones: zones, lowest: lowest, bracket: brackets[lowest], seed: settings.seed };
    }
    return zoneCache;
  }

  // The bracket 1 zones player index may start in: their faction's own, else contested
  function startFor(index) {
    var p = state.players[index];
    var world = shuffledWorld();
    if (!world || !p.roll) return null;
    var team = RACES[p.roll.race].faction === 'alliance' ? 0 : 1;
    var own = world.zones.filter(function (z) { return z.bracket === world.lowest && z.territory === team; });
    var contested = world.zones.filter(function (z) { return z.bracket === world.lowest && z.territory === 2; });
    var zones = own.length ? own : contested;
    // Like Randomizer.PlayerStart.PreferLowZones: zones that were low level before the shuffle come first
    var low = zones.filter(function (z) { return z.home === world.lowest; });
    return { zones: zones, preferred: low.length ? low : zones, own: own.length > 0, world: world };
  }

  // Keep each roll's start zone valid for the current seed (no reroll counted)
  function fixZones() {
    state.players.forEach(function (p, i) {
      if (!p.roll) return;
      var start = startFor(i);
      if (!start || !start.zones.length) { p.roll.zone = 0; return; }
      var ids = start.zones.map(function (z) { return z.id; });
      if (ids.indexOf(p.roll.zone) < 0) p.roll.zone = pickOne(start.preferred.map(function (z) { return z.id; }));
    });
  }

  function newWorldSeed() {
    var settings = worldSettings();
    settings.seed = R.randomSeed();
    try { localStorage.setItem('wr-map', JSON.stringify(settings)); } catch (e) { /* storage blocked */ }
    state.players[0].counts.world++;
    state.players.forEach(function (p) { if (p.roll) p.roll.zone = 0; });
  }

  // ---- Trades ----

  function tradeCheck(a, b, what, sa, sb) {
    var A = state.players[a];
    var B = state.players[b];
    if (!A || !B || a === b) return { msg: 'Pick two different players.' };
    if (!A.roll || !B.roll) return { msg: 'Both players need a roll first.' };
    if (locked(A) || locked(B)) return { msg: 'Hardcore rolls are locked.' };
    var ra = A.roll;
    var rb = B.roll;
    var warn = [];
    function notWanted(player, cat, key) {
      if (!isAllowed(player, cat, String(key))) warn.push(esc(player.name) + ' had ' + itemName(cat, String(key)) +
        ' turned off');
    }
    if (what === 'race') {
      if (ra.race === rb.race) return { msg: 'They have the same race.' };
      if (COMBOS[rb.race].indexOf(ra.cls) < 0) {
        return { msg: 'A ' + RACES[rb.race].name + ' can\'t be a ' + CLASSES[ra.cls].name + '.' };
      }
      if (COMBOS[ra.race].indexOf(rb.cls) < 0) {
        return { msg: 'A ' + RACES[ra.race].name + ' can\'t be a ' + CLASSES[rb.cls].name + '.' };
      }
      notWanted(A, 'race', rb.race);
      notWanted(B, 'race', ra.race);
      return { ok: true, warn: warn, msg: esc(A.name) + ' becomes a ' + RACES[rb.race].name + ', ' + esc(B.name) +
        ' a ' + RACES[ra.race].name + '.' };
    }
    if (what === 'class') {
      if (ra.cls === rb.cls) return { msg: 'They have the same class.' };
      if (COMBOS[ra.race].indexOf(rb.cls) < 0) {
        return { msg: 'A ' + RACES[ra.race].name + ' can\'t be a ' + CLASSES[rb.cls].name + '.' };
      }
      if (COMBOS[rb.race].indexOf(ra.cls) < 0) {
        return { msg: 'A ' + RACES[rb.race].name + ' can\'t be a ' + CLASSES[ra.cls].name + '.' };
      }
      notWanted(A, 'class', rb.cls);
      notWanted(B, 'class', ra.cls);
      return { ok: true, warn: warn, msg: esc(A.name) + ' becomes a ' + CLASSES[rb.cls].name + ', ' + esc(B.name) +
        ' a ' + CLASSES[ra.cls].name + ' (specs go with the classes).' };
    }
    if (what === 'spec') {
      if (ra.cls !== rb.cls) return { msg: 'Specs only trade between players of the same class.' };
      if (ra.spec < 0 || rb.spec < 0) return { msg: 'Both need a rolled spec.' };
      if (ra.spec === rb.spec) return { msg: 'They have the same spec.' };
      notWanted(A, 'spec', ra.cls + '-' + rb.spec);
      notWanted(B, 'spec', rb.cls + '-' + ra.spec);
      return { ok: true, warn: warn, msg: esc(A.name) + ' becomes ' + CLASSES[ra.cls].specs[rb.spec] + ', ' +
        esc(B.name) + ' ' + CLASSES[ra.cls].specs[ra.spec] + '.' };
    }
    var give = ra.profs[sa];
    var get = rb.profs[sb];
    if (give < 0 || get < 0) return { msg: 'Both slots need a rolled profession.' };
    if (give === get) return { msg: 'That is the same profession.' };
    if (ra.profs[1 - sa] === get) {
      return { msg: esc(A.name) + ' already has ' + PROFESSIONS[get].name + ' in the other slot.' };
    }
    if (rb.profs[1 - sb] === give) {
      return { msg: esc(B.name) + ' already has ' + PROFESSIONS[give].name + ' in the other slot.' };
    }
    notWanted(A, 'prof' + (sa + 1), get);
    notWanted(B, 'prof' + (sb + 1), give);
    return { ok: true, warn: warn, msg: esc(A.name) + ' gets ' + PROFESSIONS[get].name + ', ' + esc(B.name) + ' gets ' +
      PROFESSIONS[give].name + '.' };
  }

  function doTrade(a, b, what, sa, sb) {
    var check = tradeCheck(a, b, what, sa, sb);
    if (!check.ok) return check;
    var ra = state.players[a].roll;
    var rb = state.players[b].roll;
    var swap;
    if (what === 'race') {
      swap = [ra.race, ra.gender];
      ra.race = rb.race; ra.gender = rb.gender;
      rb.race = swap[0]; rb.gender = swap[1];
      if (RACES[ra.race].faction !== RACES[rb.race].faction) { ra.zone = 0; rb.zone = 0; }
    } else if (what === 'class') {
      swap = [ra.cls, ra.spec];
      ra.cls = rb.cls; ra.spec = rb.spec;
      rb.cls = swap[0]; rb.spec = swap[1];
    } else if (what === 'spec') {
      swap = ra.spec; ra.spec = rb.spec; rb.spec = swap;
    } else {
      swap = ra.profs[sa]; ra.profs[sa] = rb.profs[sb]; rb.profs[sb] = swap;
    }
    state.players[a].counts.trade++;
    state.players[b].counts.trade++;
    return { ok: true, msg: 'Traded. ' + check.msg };
  }

  // ---- Codes ----
  // WR4.<base64url bits>: packed to fit one chat line (255 characters) for a usual party. The JSON codes of
  // earlier versions (WRC3. / WRP3. and the first page's) still load.

  function BitWriter() { this.bits = []; }
  BitWriter.prototype.put = function (value, count) {
    for (var i = count - 1; i >= 0; i--) this.bits.push(Math.floor(value / Math.pow(2, i)) % 2);
  };
  BitWriter.prototype.flag = function (on) { this.bits.push(on ? 1 : 0); };
  // Small numbers: 4 bits at a time, with a "more" bit
  BitWriter.prototype.varint = function (value) {
    value = Math.max(0, Math.floor(value));
    do {
      var chunk = value % 16;
      value = Math.floor(value / 16);
      this.put(chunk, 4);
      this.flag(value > 0);
    } while (value > 0);
  };
  BitWriter.prototype.text = function (str) {
    var bytes = unescape(encodeURIComponent(str)).slice(0, 63);
    this.put(bytes.length, 6);
    for (var i = 0; i < bytes.length; i++) this.put(bytes.charCodeAt(i), 8);
  };
  BitWriter.prototype.toString = function () {
    var binary = '';
    for (var i = 0; i < this.bits.length; i += 8) {
      var byte = 0;
      for (var j = 0; j < 8; j++) byte = byte * 2 + (this.bits[i + j] || 0);
      binary += String.fromCharCode(byte);
    }
    return btoa(binary).replace(/\+/g, '-').replace(/\//g, '_').replace(/=+$/, '');
  };

  function BitReader(text) {
    var base64 = text.replace(/-/g, '+').replace(/_/g, '/');
    while (base64.length % 4) base64 += '=';
    var binary = atob(base64);
    this.bits = [];
    for (var i = 0; i < binary.length; i++) {
      var byte = binary.charCodeAt(i);
      for (var j = 7; j >= 0; j--) this.bits.push((byte >> j) & 1);
    }
    this.pos = 0;
  }
  BitReader.prototype.get = function (count) {
    var value = 0;
    for (var i = 0; i < count; i++) {
      if (this.pos >= this.bits.length) throw new Error('code too short');
      value = value * 2 + this.bits[this.pos++];
    }
    return value;
  };
  BitReader.prototype.flag = function () { return this.get(1) === 1; };
  BitReader.prototype.varint = function () {
    var value = 0;
    var scale = 1;
    var more = true;
    while (more) {
      value += this.get(4) * scale;
      scale *= 16;
      more = this.flag();
      if (scale > Math.pow(16, 8)) throw new Error('bad number');
    }
    return value;
  };
  BitReader.prototype.text = function () {
    var length = this.get(6);
    var bytes = '';
    for (var i = 0; i < length; i++) bytes += String.fromCharCode(this.get(8));
    return decodeURIComponent(escape(bytes));
  };

  function sameCategory(a, b) {
    return a.mode === b.mode && JSON.stringify(a.allow) === JSON.stringify(b.allow) &&
      JSON.stringify(a.weight) === JSON.stringify(b.weight) && (a.mode !== 'pick' || a.pick === b.pick);
  }

  function writeWorld(w) {
    var map = worldSettings();
    w.flag(!!map.seed);
    if (!map.seed) return;
    w.put(map.seed >>> 0, 32);
    var brackets = R.parseBrackets(map.brackets || DEFAULT_BRACKETS);
    var isDefault = brackets.map(function (b) { return b.min + '-' + b.max; }).join(',') === DEFAULT_BRACKETS;
    w.flag(isDefault);
    if (!isDefault) {
      w.put(Math.min(brackets.length, 31), 5);
      brackets.slice(0, 31).forEach(function (b) { w.put(b.min, 7); w.put(b.max, 7); });
    }
    w.put(map.scope || 0, 2);
    w.flag(!!map.balanced);
    w.flag(map.adjacent !== 0);
    w.flag(map.factionStarts !== 0);
  }

  function readWorld(r) {
    if (!r.flag()) return null;
    var seed = r.get(32);
    var brackets = DEFAULT_BRACKETS;
    if (!r.flag()) {
      var list = [];
      for (var n = r.get(5); n > 0; n--) list.push(r.get(7) + '-' + r.get(7));
      brackets = list.join(',');
    }
    return [seed, brackets, r.get(2), r.flag() ? 1 : 0, r.flag() ? 1 : 0, r.flag() ? 1 : 0];
  }

  function writePlayer(w, p, index) {
    w.flag(p.name === 'Player ' + (index + 1));
    if (p.name !== 'Player ' + (index + 1)) w.text(p.name);

    var s = p.settings;
    var d = defaultSettings();
    var plain = s.gender === d.gender && s.rollSpec === d.rollSpec && s.rollProfs === d.rollProfs &&
      s.profRule === d.profRule && CATS.every(function (cat) { return sameCategory(s.cats[cat], d.cats[cat]); });
    w.flag(plain);
    if (!plain) {
      w.put(['random', 'm', 'f'].indexOf(s.gender), 2);
      w.flag(s.rollSpec);
      w.flag(s.rollProfs);
      w.put(PROF_RULES.indexOf(s.profRule), 2);
      CATS.forEach(function (cat) {
        var c = s.cats[cat];
        var same = sameCategory(c, d.cats[cat]);
        w.flag(same);
        if (same) return;
        w.put(MODES.indexOf(c.mode), 2);
        KEYS[cat].forEach(function (key) { w.flag(!!c.allow[key]); });
        if (c.mode === 'pick') w.put(Math.max(0, KEYS[cat].indexOf(c.pick)), 5);
        var weighted = KEYS[cat].some(function (key) { return Number(c.weight[key]) !== 10; });
        w.flag(weighted);
        if (weighted) KEYS[cat].forEach(function (key) { w.varint(Number(c.weight[key]) || 0); });
      });
    }

    var r = p.roll;
    w.flag(!!r);
    if (r) {
      w.put(r.race, 4);
      w.put(r.cls, 4);
      w.flag(r.gender === 'f');
      w.put(r.spec >= 0 ? r.spec : 3, 2);
      w.put(r.profs[0] >= 0 ? r.profs[0] : 15, 4);
      w.put(r.profs[1] >= 0 ? r.profs[1] : 15, 4);
      w.flag(!!r.zone);
      if (r.zone) w.put(r.zone, 14);
    }

    COUNT_KEYS.concat('trade').forEach(function (key) {
      w.flag(p.counts[key] > 0);
      if (p.counts[key] > 0) w.varint(p.counts[key]);
    });

    // Draft bans (WR5)
    var drafted = DRAFT_CATS.some(function (k) { return p.draft[k].length > 0; });
    w.flag(drafted);
    if (drafted) {
      DRAFT_CATS.forEach(function (k) {
        var list = p.draft[k].filter(function (key) { return DRAFT_KEYS[k].indexOf(key) >= 0; }).slice(0, 15);
        w.put(list.length, 4);
        list.forEach(function (key) { w.put(DRAFT_KEYS[k].indexOf(key), 6); });
      });
    }
  }

  function readPlayer(r, index, version) {
    var p = newPlayer(index + 1);
    if (!r.flag()) p.name = r.text().slice(0, 24) || p.name;

    if (!r.flag()) {
      var s = p.settings;
      s.gender = ['random', 'm', 'f'][r.get(2)] || 'random';
      s.rollSpec = r.flag();
      s.rollProfs = r.flag();
      s.profRule = PROF_RULES[r.get(2)] || 'any';
      CATS.forEach(function (cat) {
        if (r.flag()) return;
        var c = s.cats[cat];
        c.mode = MODES[r.get(2)];
        KEYS[cat].forEach(function (key) { c.allow[key] = r.flag(); });
        if (c.mode === 'pick') c.pick = KEYS[cat][r.get(5)] || '';
        if (r.flag()) KEYS[cat].forEach(function (key) { c.weight[key] = Math.min(1000, r.varint()); });
      });
    }

    if (r.flag()) {
      var roll = { race: r.get(4), cls: r.get(4), gender: r.flag() ? 'f' : 'm', spec: r.get(2), profs: [r.get(4),
        r.get(4)], zone: 0 };
      if (roll.spec === 3) roll.spec = -1;
      roll.profs = roll.profs.map(function (prof) { return PROFESSIONS[prof] ? prof : -1; });
      if (r.flag()) roll.zone = r.get(14);
      if (!RACES[roll.race] || !CLASSES[roll.cls] || COMBOS[roll.race].indexOf(roll.cls) < 0) {
        throw new Error('bad roll');
      }
      p.roll = roll;
    }

    COUNT_KEYS.concat('trade').forEach(function (key) { p.counts[key] = r.flag() ? r.varint() : 0; });
    if (version >= 5 && r.flag()) {
      DRAFT_CATS.forEach(function (k) {
        for (var n = r.get(4); n > 0; n--) {
          var key = DRAFT_KEYS[k][r.get(6)];
          if (key) p.draft[k].push(key);
        }
      });
    }
    return normalizePlayer(p, index + 1);
  }

  function packedCode(players, party) {
    var w = new BitWriter();
    w.flag(party);
    w.put(ERAS.indexOf(state.era), 2);
    w.flag(state.hardcore);
    writeWorld(w);
    if (party) {
      var rules = state.party;
      w.put(['same', 'free', 'different'].indexOf(rules.faction), 2);
      ['race', 'cls', 'spec', 'prof'].forEach(function (k) { w.put(Math.min(63, rules.limit[k]), 6); });
      w.flag(rules.profSlot);
      w.put(['off', 'player', 'party'].indexOf(rules.compat), 2);
      var anyBan = BAN_CATS.some(function (b) { return Object.keys(rules.ban[b[0]]).some(function (k) {
        return rules.ban[b[0]][k]; }); });
      w.flag(rules.pick === 'draft');
      DRAFT_CATS.forEach(function (k) { w.put(Math.min(15, rules.draft[k]), 4); });
      w.flag(anyBan);
      if (anyBan) {
        BAN_CATS.forEach(function (b) {
          KEYS[b[1]].forEach(function (key) { w.flag(!!rules.ban[b[0]][key]); });
        });
      }
      w.put(players.length - 1, 6);
    }
    players.forEach(function (p) { writePlayer(w, p, party ? state.players.indexOf(p) : 0); });
    return 'WR5.' + w.toString();
  }

  function readPackedCode(text, version) {
    var r = new BitReader(text);
    var party = r.flag();
    var out = { era: ERAS[r.get(2)] || 'wrath', hardcore: r.flag(), rules: null, players: [] };
    out.world = readWorld(r);
    var count = 1;
    if (party) {
      if (version === 4) {
        out.rules = normalizeRules({
          faction: ['same', 'free', 'different'][r.get(2)] || 'same', dupRace: r.flag(), dupClass: r.flag(),
          dupSpec: r.flag(), profDup: ['yes', 'slot', 'no'][r.get(2)] || 'yes',
          compat: ['off', 'player', 'party'][r.get(2)] || 'off'
        });
      } else {
        var rules = defaultRules();
        rules.faction = ['same', 'free', 'different'][r.get(2)] || 'same';
        ['race', 'cls', 'spec', 'prof'].forEach(function (k) { rules.limit[k] = r.get(6); });
        rules.profSlot = r.flag();
        rules.compat = ['off', 'player', 'party'][r.get(2)] || 'off';
        rules.pick = r.flag() ? 'draft' : 'free';
        DRAFT_CATS.forEach(function (k) { rules.draft[k] = r.get(4); });
        if (r.flag()) BAN_CATS.forEach(function (b) { KEYS[b[1]].forEach(function (key) {
          if (r.flag()) rules.ban[b[0]][key] = true; }); });
        out.rules = rules;
      }
      count = r.get(6) + 1;
    }
    for (var i = 0; i < count; i++) out.players.push(readPlayer(r, i, version));
    return out;
  }

  function fromBase64(text) {
    var base64 = text.replace(/-/g, '+').replace(/_/g, '/');
    while (base64.length % 4) base64 += '=';
    return decodeURIComponent(escape(atob(base64)));
  }

  function maskOf(cat, set) {
    var value = 0;
    KEYS[cat].forEach(function (key, i) { if (set[key]) value += Math.pow(2, i); });
    return value.toString(36);
  }

  function unmask(cat, text) {
    var value = parseInt(text, 36) || 0;
    var set = {};
    KEYS[cat].forEach(function (key, i) { set[key] = Math.floor(value / Math.pow(2, i)) % 2 === 1; });
    return set;
  }

  function decodeSettings(data) {
    var s = defaultSettings();
    s.gender = ['random', 'm', 'f'][data[0]] || 'random';
    s.rollSpec = !!data[1];
    s.rollProfs = !!data[2];
    s.profRule = PROF_RULES[data[3]] || 'any';
    CATS.forEach(function (cat, i) {
      var entry = data[4][i];
      if (!entry) return;
      var c = s.cats[cat];
      c.mode = MODES[entry[0]] || 'random';
      c.allow = unmask(cat, entry[1]);
      c.pick = entry[2] >= 0 ? KEYS[cat][entry[2]] : '';
      if (entry[3]) KEYS[cat].forEach(function (key, k) { c.weight[key] = Number(entry[3][k]) || 0; });
    });
    return s;
  }

  function decodePlayer(data, number) {
    var p = newPlayer(number);
    p.name = String(data[0] || p.name).slice(0, 24);
    p.settings = decodeSettings(data[1]);
    if (data[2]) {
      var r = data[2];
      if (!RACES[r[0]] || !CLASSES[r[1]]) throw new Error('bad roll');
      p.roll = { race: r[0], cls: r[1], gender: r[2] ? 'f' : 'm', spec: r[3], profs: [r[4], r[5]], zone: r[6] || 0 };
    }
    COUNT_KEYS.concat('trade').forEach(function (key, i) { p.counts[key] = Number((data[3] || [])[i]) || 0; });
    return normalizePlayer(p, number);
  }

  function decodeRules(data) {
    return normalizeRules({
      faction: ['same', 'free', 'different'][data[0]] || 'same', dupRace: !!data[1], dupClass: !!data[2],
      dupSpec: !!data[3], profDup: ['yes', 'slot', 'no'][data[4]] || 'yes',
      compat: ['off', 'player', 'party'][data[5]] || 'off'
    });
  }

  function characterCode(p) {
    return packedCode([p], false);
  }

  function partyCode() {
    return packedCode(state.players, true);
  }

  // The party codes of the previous page: allowed lists, faction, and rolls
  function decodeOldParty(data) {
    var players = data.m.map(function (m, i) {
      var p = newPlayer(i + 1);
      var cats = p.settings.cats;
      cats.faction.allow = { alliance: !!(data.a[0] & 1), horde: !!(data.a[0] & 2) };
      KEYS.race.forEach(function (id) { cats.race.allow[id] = !!(data.a[1] & (1 << Number(id))); });
      KEYS['class'].forEach(function (id) { cats['class'].allow[id] = !!(data.a[2] & (1 << Number(id))); });
      var profs = m.slice(4).filter(function (n) { return PROFESSIONS[n]; });
      p.roll = { race: m[0], cls: m[1], gender: m[2] ? 'f' : 'm', spec: m[3], profs: [profs[0] >= 0 ? profs[0] : -1,
        profs[1] >= 0 ? profs[1] : -1], zone: 0 };
      return p;
    });
    return { era: ERAS[data.e] || 'wrath', world: data.w, players: players, rules: null, hardcore: null };
  }

  // One code -> { era, hardcore, world, rules (party codes), players }
  function decodeCode(code) {
    code = code.trim();
    var data;
    if (/^WR[45]\./.test(code)) return readPackedCode(code.slice(4), Number(code.charAt(2)));
    if (/^WR[CP]3\./.test(code)) {
      data = JSON.parse(fromBase64(code.slice(5)));
      if (code.charAt(2) === 'C') {
        return { era: ERAS[data[0]] || 'wrath', hardcore: !!data[1], world: data[2], rules: null,
          players: [decodePlayer(data[3], 1)] };
      }
      return { era: ERAS[data[0]] || 'wrath', hardcore: !!data[1], world: data[2], rules: decodeRules(data[3]),
        players: data[4].map(function (pd, i) { return decodePlayer(pd, i + 1); }) };
    }
    data = JSON.parse(fromBase64(code));
    if (data.v !== 2 || !Array.isArray(data.m)) throw new Error('not a code');
    return decodeOldParty(data);
  }

  function applyWorld(world) {
    if (!world || !world[0]) return false;
    var map = worldSettings();
    map.seed = world[0];
    map.brackets = world[1];
    map.scope = world[2];
    map.balanced = world[3];
    map.adjacent = world[4];
    map.factionStarts = world[5];
    try { localStorage.setItem('wr-map', JSON.stringify(map)); } catch (e) { /* storage blocked */ }
    return true;
  }

  function loadCodes(text, replace) {
    var codes = text.split(/\s+/).filter(Boolean);
    if (!codes.length) { message('Paste at least one code.'); return; }
    var decoded = [];
    for (var i = 0; i < codes.length; i++) {
      try { decoded.push(decodeCode(codes[i])); } catch (e) {
        message('Code ' + (i + 1) + ' is not a World Randomizer code.');
        return;
      }
    }
    var notes = [];
    var first = decoded[0];
    var players = replace ? [] : state.players.filter(function (p) { return p.roll || state.players.length > 1; });
    if (replace || !players.length) {
      state.era = first.era;
      if (first.hardcore !== null) state.hardcore = first.hardcore;
      if (first.rules) state.party = first.rules;
      if (applyWorld(first.world)) notes.push('world seed ' + first.world[0]);
    } else if (!worldSettings().seed && applyWorld(first.world)) {
      notes.push('world seed ' + first.world[0]);
    }
    decoded.forEach(function (d) {
      if (d.era !== state.era) notes.push('a code was rolled for ' + d.era + ', kept ' + state.era);
      d.players.forEach(function (p) { players.push(p); });
    });
    if (players.length > 40) players = players.slice(0, 40);
    state.players = players;
    renumberPlayers();
    state.tab = 0;
    message('Loaded ' + decoded.reduce(function (n, d) { return n + d.players.length; }, 0) + ' player(s)' +
      (notes.length ? ': ' + notes.join(', ') : '') + '.', 'good');
  }

  // ---- Rendering ----

  var $ = function (id) { return document.getElementById(id); };

  function showIcons() { return state.display !== 'names'; }
  function showNames() { return state.display !== 'icons'; }

  function icon(cat, key, gender, tip, extra) {
    return '<img class="ic' + (extra ? ' ' + extra : '') + '" src="' + itemIcon(cat, key, gender) + '" alt="' +
      esc(tip) + '" data-tip="' + esc(tip) + '">';
  }

  function tile(index, cat, label, iconHtml, name, sub, canReroll) {
    var p = state.players[index];
    var count = p.counts[cat] || 0;
    return '<div class="tile' + (name ? '' : ' empty') + '"><div class="label">' + label + '</div>' +
      (showIcons() ? '<div class="tile-ic">' + (iconHtml || '<span class="ic blank"></span>') + '</div>' : '') +
      '<div class="tname">' + (name || '&mdash;') + '</div>' +
      '<div class="sub">' + (sub || '&nbsp;') + '</div>' +
      // The reroll button always sits at the bottom, so tiles line up
      '<div class="tile-foot">' + (canReroll ? '<button class="reroll" data-act="reroll" data-cat="' + cat +
        '" data-i="' + index + '" data-tip="Reroll ' + label.toLowerCase() + ' (' + count + ' so far)">&#x21bb; ' +
        count + '</button>' : '') + '</div>' +
      '</div>';
  }

  function totalRerolls(p) {
    return COUNT_KEYS.reduce(function (sum, key) { return sum + (p.counts[key] || 0); }, 0);
  }

  function countsTip(p) {
    var parts = COUNT_KEYS.filter(function (key) { return p.counts[key]; }).map(function (key) {
      return COUNT_LABEL[key] + ' ' + p.counts[key];
    });
    if (p.counts.trade) parts.push('trades ' + p.counts.trade);
    return parts.length ? 'Rerolls: ' + parts.join(', ') : 'No rerolls yet';
  }

  function renderPlayer(p, index) {
    var r = p.roll;
    var can = !!r && !locked(p);
    var party = state.players.length > 1;
    var head = '<div class="phead">' +
      (r ? icon('faction', RACES[r.race].faction, '', itemName('faction', RACES[r.race].faction), 'faction') :
        '<span class="ic faction blank"></span>') +
      '<input type="text" class="pname" data-i="' + index + '" value="' + esc(p.name) +
      '" maxlength="24" aria-label="Name">' +
      '<span class="badge" data-tip="' + esc(countsTip(p)) + '">&#x21bb; ' + totalRerolls(p) + '</span>' +
      (r && can && !(party && state.party.faction === 'same') ? '<button class="reroll faction-reroll" ' +
        'data-act="reroll" data-cat="faction" data-i="' + index + '" data-tip="Reroll faction (' + p.counts.faction +
        ' so far)">&#x21bb; Faction</button>' : '') +
      '<button data-act="roll" data-i="' + index + '"' + (locked(p) ? ' disabled' : '') + '>' +
      (r ? 'Reroll all' : 'Roll') + '</button>' +
      '<button class="quiet" data-act="pick" data-i="' + index + '" data-tip="Pick this character yourself, step by ' +
      'step">Pick</button>' +
      '<button class="quiet" data-act="prefs" data-i="' + index + '" data-tip="Advanced preferences for random rolls ' +
      '(chances, shuffle...)">&#x2699;</button>' +
      '<button class="quiet" data-act="copy" data-i="' + index + '" data-tip="Copy this player\'s code">Code</button>' +
      (r ? '<button class="quiet" data-act="clear" data-i="' + index + '" data-tip="Clear this roll and its reroll ' +
        'counts (keeps the player and their preferences)">&#x21ba;</button>' : '') +
      (index > 0 ? '<button class="quiet" data-act="remove" data-i="' + index +
        '" data-tip="Remove">&#x2715;</button>' :
        '') + '</div>';

    if (!r) {
      return '<div class="player">' + head + '<div class="tiles">' +
        tile(index, 'race', 'Race', '', '', '', false) + tile(index, 'class', 'Class', '', '', '', false) +
        tile(index, 'spec', 'Spec', '', '', '', false) + tile(index, 'prof1', 'Profession 1', '', '', '', false) +
        tile(index, 'prof2', 'Profession 2', '', '', '', false) + '</div></div>';
    }

    var race = RACES[r.race];
    var cls = CLASSES[r.cls];
    var gender = r.gender === 'f' ? 'Female' : 'Male';
    var specKey = r.cls + '-' + r.spec;
    var tiles =
      tile(index, 'race', 'Race', icon('race', r.race, r.gender, race.name + ' (' + gender + ')'), race.name, gender,
        can) +
      tile(index, 'class', 'Class', icon('class', r.cls, '', cls.name), cls.name, '', can) +
      tile(index, 'spec', 'Spec', r.spec >= 0 ? icon('spec', specKey, '', itemName('spec', specKey)) : '',
        r.spec >= 0 ? cls.specs[r.spec] : '', r.spec >= 0 ? SPEC_ROLES[r.cls][r.spec] : 'not rolled',
        can && p.settings.rollSpec) +
      [0, 1].map(function (slot) {
        var prof = r.profs[slot];
        return tile(index, 'prof' + (slot + 1), 'Profession ' + (slot + 1),
          prof >= 0 ? icon('prof' + (slot + 1), String(prof), '', PROFESSIONS[prof].name) : '',
          prof >= 0 ? PROFESSIONS[prof].name : '', prof >= 0 ? (PROFESSIONS[prof].gather ? 'gathering' : 'crafting') :
            'not rolled', can && p.settings.rollProfs);
      }).join('');

    return '<div class="player' + (locked(p) ? ' locked-player' : '') + '">' + head +
      '<div class="tiles">' + tiles + '</div>' + startLine(index) +
      '<div class="pstart small" data-tip="Ties the character to this roll for good: talents only in the rolled ' +
      'tree, trainers only teach the rolled professions (and it learns them, with their tools). The race and class ' +
      'must match.">In game: <code>/randach roll</code> (addon) or <code>.randomizer roll &lt;party code&gt; ' +
      (index + 1) + '</code></div>' +
      (locked(p) ? '<div class="pnote"><span class="locked">Hardcore roll locked.</span> One life, no rerolls.</div>' :
        '') + '</div>';
  }

  function zoneName(id) {
    var zone = (window.RANDOMIZER_ZONES || []).filter(function (z) { return z.id === id; })[0];
    return zone ? zone.name : 'zone ' + id;
  }

  function startLine(index) {
    var p = state.players[index];
    var r = p.roll;
    if (r.cls === 6) return '<div class="pstart">Starts in Acherus: The Ebon Hold, as Death Knights do.</div>';
    var leader = state.players[0];
    if (index > 0 && leader.roll) {
      var sameFaction = RACES[leader.roll.race].faction === RACES[r.race].faction;
      return '<div class="pstart">' + (sameFaction ? 'Joins ' + esc(leader.name) + ' with <code>.coop join</code>' :
        '<span class="bad">Other faction than ' + esc(leader.name) + ': co-op needs one faction.</span>') + '</div>';
    }
    var start = startFor(index);
    if (!start) return '<div class="pstart muted">Start zone: needs a world seed (above).</div>';
    if (!start.zones.length) {
      return '<div class="pstart muted">No bracket 1 zone fits this faction with this seed; the server falls back ' +
        'to the normal start.</div>';
    }
    var all = start.zones.map(function (z) { return z.name + ' (' + z.id + ')'; }).join(', ');
    var auto = start.preferred.map(function (z) { return z.name; }).join(', ');
    return '<div class="pstart">Starts in <b data-tip="' + esc('Bracket 1 zones: ' + all) + '">' +
      esc(zoneName(r.zone)) + '</b>' + (start.own ? '' : ' <span class="muted">(contested)</span>') +
      (locked(p) || start.zones.length < 2 ? '' :
        ' <button class="reroll" data-act="reroll" data-cat="zone" data-i="' + index +
        '" data-tip="Reroll start zone (' + p.counts.zone + ' so far)">&#x21bb; ' + p.counts.zone + '</button>') +
      '<div class="small">To start exactly there, at level 1 type <code>.coop create ' + r.zone + '</code> ' +
      '(alone too). <span class="muted">Otherwise the server picks one of: ' + esc(auto) + '.</span></div>' +
      '</div>';
  }

  function renderPlayers() {
    fixZones();
    $('players').innerHTML = state.players.map(renderPlayer).join('');
  }

  function renderWorld() {
    var world = worldSettings();
    var leader = state.players[0];
    $('world').innerHTML = 'World seed: ' + (world.seed ? '<b>' + world.seed + '</b>' : '<i>none yet</i>') +
      ' <button class="quiet small-btn" data-act="seed" data-tip="New world seed (' + leader.counts.world +
      ' so far). On the server: .randomizer reroll &lt;seed&gt; and a restart.">&#x21bb; New seed</button> ' +
      '<a href="map.html">see it on the map</a>' +
      (world.seed ? ' &middot; server: <code>.randomizer reroll ' + world.seed + '</code>' : '');
    $('msg').innerHTML = lastMessage;
  }

  function renderTrade() {
    var options = state.players.map(function (p, i) {
      return '<option value="' + i + '">' + esc(p.name) + '</option>';
    }).join('');
    ['trade-a', 'trade-b'].forEach(function (id, n) {
      var select = $(id);
      var previous = select.value;
      select.innerHTML = options;
      select.value = previous !== '' && Number(previous) < state.players.length ? previous : String(Math.min(n,
        state.players.length - 1));
    });
    if ($('trade-a').value === $('trade-b').value && state.players.length > 1) {
      $('trade-b').value = String(Number($('trade-a').value) === 0 ? 1 : 0);
    }
    var isProf = $('trade-what').value === 'prof';
    $('trade-sa-box').style.display = isProf ? '' : 'none';
    $('trade-sb-box').style.display = isProf ? '' : 'none';
    $('trade-panel').style.display = state.players.length > 1 ? '' : 'none';
    updateTradePreview();
  }

  function tradeArgs() {
    return [Number($('trade-a').value), Number($('trade-b').value), $('trade-what').value, Number($('trade-sa').value),
      Number($('trade-sb').value)];
  }

  function updateTradePreview(result) {
    var check = result || tradeCheck.apply(null, tradeArgs());
    $('trade-msg').innerHTML = (check.ok ? '<span class="good">' + check.msg + '</span>' : check.msg) +
      (check.warn && check.warn.length ? ' <span class="muted">(' + check.warn.join('; ') + ')</span>' : '');
    $('trade-go').disabled = !check.ok || !!result;
  }

  function percentages(p, cat) {
    var s = p.settings.cats[cat];
    var keys = allowedKeys(p, cat);
    var total = 0;
    keys.forEach(function (key) { total += Math.max(0, Number(s.weight[key]) || 0); });
    var out = {};
    keys.forEach(function (key) {
      out[key] = total > 0 ? Math.round(1000 * Math.max(0, Number(s.weight[key]) || 0) / total) / 10 : 0;
    });
    return out;
  }

  // Why an item can't be chosen at all here, '' if it can: banned, or its faction / class is off for this player
  function blockedReason(p, cat, key) {
    if (banned(cat, key)) return 'banned by the rules';
    if (cat === 'race' && !isAllowed(p, 'faction', RACES[key].faction)) {
      return 'the ' + itemName('faction', RACES[key].faction) + ' is off for ' + p.name;
    }
    if (cat === 'spec') {
      var cls = key.split('-')[0];
      if (!isAllowed(p, 'class', cls)) return CLASSES[cls].name + ' is off for ' + p.name;
    }
    return '';
  }

  function prefItem(p, cat, key, pct) {
    var s = p.settings.cats[cat];
    var era = inEra(cat, key, state.era);
    var blocked = blockedReason(p, cat, key);
    var off = blocked || (s.mode === 'pick' ? s.pick !== key : !s.allow[key]);
    var slashed = blocked || !s.allow[key];
    var name = itemName(cat, key);
    var tip = name + (blocked ? ' (' + blocked + ')' : '') + (era ? '' : ' (' + ERA_NAME[since(cat, key)] + ')') +
      (s.mode === 'chances' && !off && era ? ': ' + (pct[key] || 0) + '%' : '') +
      (s.mode === 'shuffle' && !off && era ? (deckOf(p, cat).indexOf(key) >= 0 ? ': still in the deck' :
        ': already drawn') : '');
    var shortName = cat === 'spec' ? CLASSES[key.split('-')[0]].specs[key.split('-')[1]] : name;
    var gender = p.settings.gender === 'f' ? 'f' : 'm';
    return '<div class="item' + (off ? ' off' : '') + (era ? '' : ' era') + (slashed ? ' slashed' : '') +
      (blocked ? ' blocked' : '') + (s.mode === 'pick' && !off ? ' picked' : '') +
      (s.mode === 'shuffle' && !off && era && deckOf(p, cat).indexOf(key) < 0 ? ' drawn' : '') +
      '" data-act="item" data-cat="' + cat + '" data-key="' + key + '" data-tip="' + esc(tip) + '">' +
      (showIcons() ? '<span class="icw"><img src="' + itemIcon(cat, key, gender) + '" alt="' + esc(name) + '"></span>' :
        '') +
      (showNames() ? '<span class="iname">' + esc(shortName) + '</span>' : '') +
      (s.mode === 'chances' ? '<input type="number" class="w" min="0" max="1000" data-cat="' + cat + '" data-key="' +
        key + '" value="' + (Number(s.weight[key]) || 0) + '" aria-label="' + esc(name) + ' chance">' +
        '<span class="pct">' + (off || !era ? '&ndash;' : (pct[key] || 0) + '%') + '</span>' : '') +
      '</div>';
  }

  function renderPrefs() {
    var index = state.tab;
    var p = state.players[index];
    $('prefs-title').textContent = 'Advanced preferences: ' + p.name;
    $('prefs-tabs').innerHTML = state.players.length > 1 ? state.players.map(function (q, i) {
      return '<button class="' + (i === index ? '' : 'quiet') + '" data-act="tab" data-i="' + i + '">' + esc(q.name) +
        '</button>';
    }).join('') : '';

    var s = p.settings;
    var html = '<p class="muted" style="margin:0 0 10px">What ' + esc(p.name) + ' may get, and how it is rolled. ' +
      'Click an icon to turn it on or off (or, in Pick, to choose it). Death Knights are off by default: they start ' +
      'at level 55 in Ebon Hold.</p>' +
      '<div class="controls">' +
      '<label class="field"><span>Gender</span><select data-pref="gender">' +
      [['random', 'Random'], ['m', 'Male'], ['f', 'Female']].map(function (o) {
        return '<option value="' + o[0] + '"' + (s.gender === o[0] ? ' selected' : '') + '>' + o[1] + '</option>';
      }).join('') + '</select></label>' +
      '<label class="check"><input type="checkbox" data-pref="rollSpec"' + (s.rollSpec ? ' checked' : '') +
      '> Roll a spec</label>' +
      '<label class="check"><input type="checkbox" data-pref="rollProfs"' + (s.rollProfs ? ' checked' : '') +
      '> Roll two professions</label>' +
      '<label class="field"><span>Professions must include</span><select data-pref="profRule">' +
      [['any', 'Anything'], ['gather', 'At least one gathering'], ['craft', 'At least one crafting'],
        ['both', 'One gathering and one crafting']].map(function (o) {
        return '<option value="' + o[0] + '"' + (s.profRule === o[0] ? ' selected' : '') + '>' + o[1] + '</option>';
      }).join('') + '</select></label>' +
      '</div>';

    CATS.forEach(function (cat) {
      if (cat === 'spec' && !s.rollSpec) return;
      if ((cat === 'prof1' || cat === 'prof2') && !s.rollProfs) return;
      var c = s.cats[cat];
      var pct = c.mode === 'chances' ? percentages(p, cat) : {};
      var items;
      if (cat === 'spec') {
        items = KEYS['class'].map(function (cls) {
          var classOff = !isAllowed(p, 'class', cls);
          return '<div class="specgroup' + (classOff ? ' class-off' : '') + '">' +
            (showIcons() ? '<img class="classmark" src="' + itemIcon('class', cls) + '" alt="" data-tip="' +
              esc(CLASSES[cls].name + (classOff ? ' (class turned off)' : '')) + '">' :
              '<span class="classname">' + esc(CLASSES[cls].name) + '</span>') +
            [0, 1, 2].map(function (i) { return prefItem(p, cat, cls + '-' + i, pct); }).join('') + '</div>';
        }).join('');
      } else {
        items = KEYS[cat].map(function (key) { return prefItem(p, cat, key, pct); }).join('');
      }
      var deckInfo = '';
      if (c.mode === 'shuffle') {
        deckInfo = ' <span class="muted small">' + deckOf(p, cat).length + ' of ' + allowedKeys(p, cat).length +
          ' left in the deck</span> <button class="quiet small-btn" data-act="deck" data-cat="' + cat +
          '">Reshuffle</button>';
      }
      html += '<div class="catrow"><div class="cathead"><b>' + CAT_LABEL[cat] + '</b><span class="segmented">' +
        MODES.map(function (mode) {
          return '<button class="' + (c.mode === mode ? 'on' : '') + '" data-act="mode" data-cat="' + cat +
            '" data-mode="' + mode + '" data-tip="' + MODE_TIP[mode] + '">' + MODE_LABEL[mode] + '</button>';
        }).join('') + '</span>' + deckInfo +
        (c.mode !== 'pick' ? ' <button class="quiet small-btn" data-act="all-on" data-cat="' + cat +
          '">All on</button>' : '') +
        '</div><div class="items' + (cat === 'spec' ? ' specs' : '') + '">' + items + '</div></div>';
    });

    html += '<p style="margin:12px 0 0"><button class="quiet" data-act="prefs-default">Reset ' + esc(p.name) +
      '\'s preferences</button> ' + (state.players.length > 1 ? '<button class="quiet" ' +
      'data-act="prefs-default-all">Reset everyone\'s preferences</button> ' : '') +
      (state.players.length > 1 ? '<button class="quiet" data-act="prefs-copy">Use these for every player</button>' :
        '') + '</p>';
    $('prefs').innerHTML = html;
  }

  function banItem(bcat, cat, key) {
    var on = !!state.party.ban[bcat][key];
    // Banned with its faction / class, or in the draft
    var drafter = bcat === 'faction' ? null : draftedBy(bcat, key) ||
      (cat === 'spec' ? draftedBy('cls', key.split('-')[0]) : null);
    var implied = !on && ((cat === 'race' && state.party.ban.faction[RACES[key].faction]) ||
      (cat === 'spec' && state.party.ban.cls[key.split('-')[0]]) || !!drafter);
    var name = itemName(cat, key);
    var shortName = cat === 'spec' ? CLASSES[key.split('-')[0]].specs[key.split('-')[1]] : name;
    return '<div class="item' + (on || implied ? ' off slashed' : '') + (implied ? ' blocked' : '') +
      (inEra(cat, key, state.era) ? '' : ' era') + '" data-act="' + (implied ? '' : 'ban') + '" data-bcat="' +
      bcat +
      '" data-key="' + key + '" data-tip="' + esc(name + (on ? ': banned' : drafter ? ': banned by ' + drafter.name +
        ' in the draft' : implied ? ': banned with its ' + (cat === 'race' ? 'faction' : 'class') : '')) + '">' +
      (showIcons() ? '<span class="icw"><img src="' + itemIcon(cat, key, 'm') + '" alt="' +
        esc(name) + '"></span>' : '') + (showNames() ? '<span class="iname">' + esc(shortName) + '</span>' : '') +
      '</div>';
  }

  function renderBans() {
    var labels = { faction: 'Factions', race: 'Races', cls: 'Classes', spec: 'Specs', prof: 'Professions' };
    $('bans').innerHTML = BAN_CATS.map(function (b) {
      var items;
      if (b[0] === 'spec') {
        items = KEYS['class'].map(function (cls) {
          return '<div class="specgroup">' + (showIcons() ? '<img class="classmark" src="' + itemIcon('class', cls) +
            '" alt="" data-tip="' + esc(CLASSES[cls].name) + '">' : '<span class="classname">' +
            esc(CLASSES[cls].name) + '</span>') + [0, 1, 2].map(function (i) {
            return banItem('spec', 'spec', cls + '-' + i);
          }).join('') + '</div>';
        }).join('');
      } else {
        items = KEYS[b[1]].map(function (key) { return banItem(b[0], b[1], key); }).join('');
      }
      return '<div class="banrow"><span class="banlabel">' + labels[b[0]] + '</span><div class="items' +
        (b[0] === 'spec' ? ' specs' : '') + '">' + items + '</div></div>';
    }).join('');
  }

  function renderSetup() {
    $('era').value = state.era;
    $('hardcore').value = state.hardcore ? '1' : '0';
    $('display').value = state.display;
    $('rule-faction').value = state.party.faction;
    $('limit-race').value = state.party.limit.race;
    $('limit-class').value = state.party.limit.cls;
    $('limit-spec').value = state.party.limit.spec;
    $('limit-prof').value = state.party.limit.prof;
    $('prof-slot').checked = state.party.profSlot;
    $('rule-pick').value = state.party.pick;
    DRAFT_CATS.forEach(function (k) { $('draft-' + k).value = state.party.draft[k]; });
    $('draft-box').style.display = state.party.pick === 'draft' ? '' : 'none';
    $('compat').value = state.party.compat;
    $('roll-all').textContent = state.players.length > 1 ? 'Roll everyone' : 'Roll';
  }

  function render() {
    renderSetup();
    renderBans();
    renderPlayers();
    renderWorld();
    renderTrade();
    renderPrefs();
    var code = partyCode();
    $('party-code').value = code;
    $('party-code-size').innerHTML = code.length + ' characters' + (code.length > 255 ?
      ' <span class="bad">(too long for one chat line: share the players\' own codes instead)</span>' :
      ' (fits in one chat line)');
    save();
  }

  // ---- Character picker (a game-like character creation; the preferences panel stays for random rolls) ----

  var STEP_LABEL = { player: 'Player', draft: 'Draft bans', race: 'Faction & race', 'class': 'Class & spec',
    profs: 'Professions', confirm: 'Confirm' };

  // The draft step only when the rules say so
  function wizardSteps() {
    return state.party.pick === 'draft' ? ['player', 'draft', 'race', 'class', 'profs', 'confirm'] :
      ['player', 'race', 'class', 'profs', 'confirm'];
  }

  // The character creation screen's order
  var BANNER_RACES = { alliance: ['1', '3', '4', '7', '11'], horde: ['2', '5', '6', '8', '10'] };

  // A line about each race and class, and the racials the icons don't spell out
  var RACE_INFO = {
    1: { desc: 'Versatile and stubborn, humans rebuilt Stormwind and lead the Alliance.',
      more: ['Expertise with swords and maces'] },
    2: { desc: 'Freed from demonic corruption, the orcs forged a new Horde in Durotar.',
      more: ['Pets deal more damage (Command)'] },
    3: { desc: 'Sturdy mountain folk of Ironforge, born miners, smiths and explorers.',
      more: ['Expertise with maces', 'Finds treasure on the minimap'] },
    4: { desc: 'Ancient guardians of Kalimdor\'s forests, at home in the shadows.',
      more: ['Harder to hit (Quickness)'] },
    5: { desc: 'Undead who broke free of the Lich King and claim Lordaeron as their own.',
      more: ['+10 Shadow resistance'] },
    6: { desc: 'Gentle giants of the plains who walk in harmony with the Earth Mother.',
      more: ['+10 Nature resistance'] },
    7: { desc: 'Brilliant inventors, small in size and big on ideas (and explosions).',
      more: ['+10 Arcane resistance'] },
    8: { desc: 'Cunning jungle trolls of the Darkspear tribe, sworn to the Horde.',
      more: ['Crit with bows and thrown weapons', 'Movement impairing effects 15% shorter'] },
    10: { desc: 'Proud elves of Quel\'Thalas, drawn to magic in all its forms.',
      more: [] },
    11: { desc: 'Exiles from Argus who crashed on Azeroth and joined the Alliance.',
      more: ['Jewelcrafting +5'] }
  };
  var CLASS_INFO = {
    1: 'Plate armor and big weapons: charges in, takes hits or deals them.',
    2: 'Holy warrior in plate who heals, protects or smites.',
    3: 'Ranged fighter with a loyal pet, traps and a keen eye.',
    4: 'Stealthy melee fighter with poisons, combo points and tricks.',
    5: 'Healer of the party, or a master of shadow magic.',
    6: 'Undead knight of runes and diseases; starts at level 55 in Ebon Hold.',
    7: 'Calls on the elements with totems: lightning, melee or healing.',
    8: 'Arcane, fire and frost spells from range, and food and portals for all.',
    9: 'Curses, demons and fire from range; summons a demon to help.',
    11: 'Shapeshifter who can tank, heal, or fight in melee or with spells.'
  };

  function roleSummary(cls) {
    var roles = [];
    SPEC_ROLES[cls].forEach(function (r) {
      r.split(' / ').forEach(function (one) { if (roles.indexOf(one) < 0) roles.push(one); });
    });
    return roles.join(', ');
  }

  // Other players who have it (for the picker: you see what the party already is)
  function takenBy(i, test) {
    return state.players.filter(function (q, n) { return n !== i && q.roll && test(q.roll); })
      .map(function (q) { return q.name; });
  }
  var wiz = null;

  // What player i may pick: the rules (bans, limits, faction rule) and the era; their own preferences only steer
  // random rolls
  function pickContext(i) {
    return partyContext(i);
  }

  function factionChoices(i) {
    var ctx = pickContext(i);
    return KEYS.faction.map(function (f) {
      var why = banned('faction', f) ? 'banned by the rules' :
        ctx.faction && ctx.faction !== f ? 'the party is ' + itemName('faction', ctx.faction) + ' (same faction rule)' :
          ctx.notFaction[f] ? 'another player has it (all different rule)' :
            !raceChoices(i, f).some(function (r) { return !r.why; }) ? 'no race of it is left' : '';
      return { key: f, why: why };
    });
  }

  function raceChoices(i, faction) {
    var ctx = pickContext(i);
    return KEYS.race.filter(function (r) { return RACES[r].faction === faction && inEra('race', r, state.era); })
      .map(function (r) {
        var why = banned('race', r) ? 'banned by the rules' : ctx.race[r] ? 'the party already has enough' :
          !classChoices(i, r).some(function (c) { return !c.why; }) ? 'no class of it is left' : '';
        return { key: r, why: why };
      });
  }

  function classChoices(i, race) {
    var ctx = pickContext(i);
    return COMBOS[race].map(String).filter(function (c) { return inEra('class', c, state.era); }).map(function (c) {
      var why = banned('class', c) ? 'banned by the rules' : ctx.cls[c] ? 'the party already has enough' :
        !specChoices(i, c).some(function (sp) { return !sp.why; }) ? 'no spec of it is left' : '';
      return { key: c, why: why };
    });
  }

  function specChoices(i, cls) {
    var ctx = pickContext(i);
    return [0, 1, 2].map(function (n) {
      var key = cls + '-' + n;
      return { key: key, why: banned('spec', key) ? 'banned by the rules' : ctx.spec[key] ?
        'the party already has enough' : '' };
    });
  }

  function profChoices(i, slot, other) {
    var ctx = pickContext(i);
    return KEYS.prof1.filter(function (k) { return inEra('prof1', k, state.era) && !banned('prof1', k); })
      .map(function (k) {
        var prof = Number(k);
        var why = prof === other ? 'already picked' : ctx.profAny[prof] ? 'the party already has enough' :
          ctx['prof' + slot][prof] ? 'another player has it in this slot' :
            other >= 0 && state.party.compat === 'player' && !professionsGoTogether(prof, other) ?
              'doesn\'t go with ' + PROFESSIONS[other].name + ' (compatible professions rule)' : '';
        return { key: k, why: why };
      });
  }

  function professionNote(prof) {
    var p = PROFESSIONS[prof];
    if (p.gather) {
      var feeds = PROFESSIONS.filter(function (x) { return x.needs === p.name; }).map(function (x) { return x.name; });
      return 'Gathering. Supplies ' + feeds.join(', ') + '.';
    }
    if (p.needs) return 'Crafting. Goes well with ' + p.needs + ' (its materials).';
    if (p.name === 'Tailoring') return 'Crafting. Cloth drops from humanoids; goes well with Enchanting.';
    return 'Crafting. Disenchants items you don\'t need; goes well with Tailoring.';
  }

  function openWizard(index) {
    var p = index >= 0 ? state.players[index] : null;
    var r = p && p.roll;
    wiz = {
      step: p ? wizardSteps()[1] : 'player', player: p ? index : -1, faction: r ? RACES[r.race].faction : '',
      race: r ? String(r.race) : '', gender: r ? r.gender : (p && p.settings.gender === 'f' ? 'f' : 'm'),
      cls: r ? String(r.cls) : '', spec: r && r.spec >= 0 ? r.cls + '-' + r.spec : '',
      prof1: r ? r.profs[0] : -1, prof2: r ? r.profs[1] : -1, also: 'none', note: '', hover: null
    };
    if (p && locked(p)) {
      wiz.step = 'player';
      wiz.note = '<span class="bad">' + esc(p.name) + '\'s hardcore roll is locked.</span>';
    }
    enterStep(wiz.step);
  }

  function closeWizard() {
    wiz = null;
    $('wizard').innerHTML = '';
    $('wizard').style.display = 'none';
  }

  function enterStep(step) {
    wiz.step = step;
    wiz.hover = null;
    renderWizard();
  }

  function stepReady(step) {
    if (step === 'player') return wiz.player >= 0;
    if (step === 'race') return !!wiz.race;
    if (step === 'class') return !!wiz.cls && !!wiz.spec;
    if (step === 'profs') return wiz.prof1 >= 0 && wiz.prof2 >= 0;
    return true;
  }

  function wizCard(act, key, selected, why, inner, tip) {
    return '<div class="wcard' + (selected ? ' sel' : '') + (why ? ' no' : '') + '" data-act="' + (why ? '' : act) +
      '" data-key="' + key + '" data-tip="' + esc(tip + (why ? ' (' + why + ')' : '')) + '">' + inner + '</div>';
  }

  function specsTip(cls) {
    return CLASSES[cls].name + ': ' + CLASSES[cls].specs.map(function (sp, n) {
      return sp + ' (' + SPEC_ROLES[cls][n] + ')';
    }).join(', ');
  }

  // The right-hand panel: the hovered race or class, else the selected one
  function wizDetail() {
    var w = wiz;
    var i = w.player;
    var hover = w.hover;
    if (w.step === 'race') {
      var race = hover && hover.type === 'race' ? hover.key : w.race;
      if (!race) return '<p class="muted">Hover a race to see its racials and classes; click it to choose it.</p>';
      var faction = RACES[race].faction;
      var fc = factionChoices(i).filter(function (x) { return x.key === faction; })[0];
      var rc = raceChoices(i, faction).filter(function (x) { return x.key === race; })[0];
      var why = fc.why || (rc && rc.why);
      var players = takenBy(i, function (r) { return String(r.race) === race; });
      var classes = classChoices(i, race);
      return '<div class="wd-head"><img src="' + itemIcon('race', race, w.gender) + '" alt=""><div><h3>' +
        RACES[race].name + '</h3><span class="muted small">' + itemName('faction', faction) + '</span>' +
        (why ? '<div class="bad small">Not available: ' + esc(why) + '</div>' : '') +
        (players.length ? '<div class="small">Already in the party: ' + players.map(esc).join(', ') + '</div>' : '') +
        '</div></div><p>' + RACE_INFO[race].desc + '</p><h4>Racials</h4><ul class="wd-list">' +
        (RACIALS[race] || []).map(function (rac) {
          return '<li><img src="img/racial/' + rac[1] + '.jpg" alt=""><span><b>' + rac[0] + '</b> ' + rac[2] +
            '</span></li>';
        }).join('') + RACE_INFO[race].more.map(function (line) {
          return '<li><span class="dot"></span><span>' + line + '</span></li>';
        }).join('') + '</ul><h4>Classes</h4><div class="wd-classes">' + classes.map(function (c) {
          return '<div class="wd-class' + (c.why ? ' no' : '') + '" data-tip="' + esc(CLASSES[c.key].name +
            (c.why ? ' (' + c.why + ')' : '')) + '"><img src="' + itemIcon('class', c.key) + '" alt=""><div><b>' +
            CLASSES[c.key].name + '</b> <span class="muted small">' + roleSummary(c.key) + '</span>' +
            '<div class="wd-specs">' +
            CLASSES[c.key].specs.map(function (sp, n) {
              return '<span data-tip="' + esc(sp + ': ' + SPEC_ROLES[c.key][n]) + '"><img src="' +
                itemIcon('spec', c.key + '-' + n) + '" alt="">' + sp + '</span>';
            }).join('') + '</div></div></div>';
        }).join('') + '</div>';
    }
    var cls = hover && hover.type === 'class' ? hover.key : w.cls;
    if (!cls) return '<p class="muted">Hover a class to see its specs; click a spec to choose it.</p>';
    var cc = classChoices(i, w.race).filter(function (x) { return x.key === cls; })[0];
    var inParty = takenBy(i, function (r) { return String(r.cls) === cls; });
    return '<div class="wd-head"><img src="' + itemIcon('class', cls) + '" alt=""><div><h3>' + CLASSES[cls].name +
      '</h3><span class="muted small">' + roleSummary(cls) + '</span>' +
      (cc && cc.why ? '<div class="bad small">Not available: ' + esc(cc.why) + '</div>' : '') +
      (inParty.length ? '<div class="small">Already in the party: ' + inParty.map(esc).join(', ') + '</div>' : '') +
      '</div></div><p>' + CLASS_INFO[cls] + '</p><h4>Specs</h4><div class="wd-spec-cards">' +
      specChoices(i, cls).map(function (sp) {
        var n = Number(sp.key.split('-')[1]);
        var why = (cc && cc.why) || sp.why;
        var who = takenBy(i, function (r) { return r.cls + '-' + r.spec === sp.key; });
        return '<div class="wcard' + (w.spec === sp.key ? ' sel' : '') + (why ? ' no' : '') + '" data-act="' +
          (why ? '' : 'w-spec') + '" data-key="' + sp.key + '" data-tip="' + esc(CLASSES[cls].specs[n] +
          (why ? ' (' + why + ')' : '')) + '"><img src="' + itemIcon('spec', sp.key) + '" alt=""><b>' +
          CLASSES[cls].specs[n] + '</b><span class="role">' + SPEC_ROLES[cls][n] + '</span>' +
          (who.length ? '<span class="muted small">' + who.map(esc).join(', ') + ' has it</span>' : '') +
          (why ? '<span class="bad small">' + esc(why) + '</span>' : '') + '</div>';
      }).join('') + '</div>';
  }

  // Random within what is open: a race, a class and spec, or two professions
  function wizardRandom() {
    var w = wiz;
    var i = w.player;
    if (w.step === 'race') {
      var open = [];
      factionChoices(i).forEach(function (f) {
        if (!f.why) raceChoices(i, f.key).forEach(function (r) { if (!r.why) open.push(r.key); });
      });
      if (!open.length) { w.note = 'No race is open.'; return; }
      w.race = pickOne(open);
      w.faction = RACES[w.race].faction;
      w.cls = ''; w.spec = '';
      w.note = 'Random race: ' + RACES[w.race].name + '.';
    } else if (w.step === 'class') {
      var classes = classChoices(i, w.race).filter(function (c) { return !c.why; });
      if (!classes.length) { w.note = 'No class is open for this race.'; return; }
      w.cls = pickOne(classes).key;
      w.spec = pickOne(specChoices(i, w.cls).filter(function (sp) { return !sp.why; })).key;
      w.note = 'Random: ' + itemName('spec', w.spec) + '.';
    } else if (w.step === 'profs') {
      var pairs = [];
      profChoices(i, 1, -1).forEach(function (a) {
        if (a.why) return;
        profChoices(i, 2, Number(a.key)).forEach(function (b) {
          if (!b.why) pairs.push([Number(a.key), Number(b.key)]);
        });
      });
      if (!pairs.length) { w.note = 'No two professions fit the rules.'; return; }
      var pair = pickOne(pairs);
      w.prof1 = pair[0]; w.prof2 = pair[1];
      w.note = 'Random: ' + PROFESSIONS[pair[0]].name + ' and ' + PROFESSIONS[pair[1]].name + '.';
    }
  }

  function renderWizard() {
    var w = wiz;
    var i = w.player;
    var p = i >= 0 ? state.players[i] : null;
    var body = '';

    if (w.step === 'player') {
      body = '<p class="muted">Who are you picking for? If your player isn\'t here, add them first.</p>' +
        '<div class="wgrid">' + state.players.map(function (q, n) {
          var r = q.roll;
          var why = locked(q) ? 'hardcore roll locked' : '';
          return wizCard('w-player', n, n === i, why, '<b>' + esc(q.name) + '</b><span class="muted small">' +
            (r ? RACES[r.race].name + ' ' + CLASSES[r.cls].name : 'no character yet') + '</span>', q.name);
        }).join('') + '</div><p><button class="quiet small-btn" data-act="w-add">Add a player</button></p>';
    } else if (w.step === 'draft') {
      var limits = state.party.draft;
      var dlabels = { race: 'Races', cls: 'Classes', spec: 'Specs', prof: 'Professions' };
      var dcats = { race: 'race', cls: 'class', spec: 'spec', prof: 'prof1' };
      body = '<p class="muted">Draft: ban up to ' + DRAFT_CATS.map(function (k) {
        return limits[k] + ' ' + dlabels[k].toLowerCase();
      }).join(', ') + ' for everyone. Bans are optional; click a slashed one to take it back.</p>' +
        DRAFT_CATS.filter(function (k) { return limits[k] > 0; }).map(function (k) {
          var cat = dcats[k];
          var used = p.draft[k].length;
          var keys = KEYS[cat].filter(function (key) { return inEra(cat, key, state.era); });
          return '<h3 class="wsub">' + dlabels[k] + ' <span class="muted small">(' + used + ' of ' + limits[k] +
            ' banned)</span></h3><div class="items' + (k === 'spec' ? ' specs' : '') + '">' + keys.map(function (key) {
            var mine = p.draft[k].indexOf(key) >= 0;
            var by = mine ? null : draftedBy(k, key);
            var rule = !mine && !by && banned(cat, key);
            var full = !mine && used >= limits[k];
            var why = by ? 'banned by ' + by.name : rule ? 'banned by the rules' : full ? 'no bans left' : '';
            var name = itemName(cat, key);
            return '<div class="item' + (mine || by || rule ? ' off slashed' : '') + (why ? ' blocked' : '') +
              '" data-act="' + (why ? '' : 'w-draft') + '" data-key="' + k + ':' + key + '" data-tip="' +
              esc(name + (mine ? ': banned by you' : why ? ': ' + why : '')) + '"><span class="icw"><img src="' +
              itemIcon(cat, key, 'm') + '" alt=""></span>' +
              (showNames() ? '<span class="iname">' + esc(cat === 'spec' ?
                CLASSES[key.split('-')[0]].specs[key.split('-')[1]] : name) + '</span>' : '') + '</div>';
          }).join('') + '</div>';
        }).join('');
    } else if (w.step === 'race') {
      var factions = factionChoices(i);
      var genders = [['m', 'Male'], ['f', 'Female'], ['random', 'Random']];
      body = '<div class="wtoolbar">Gender: ' + genders.map(function (g) {
        return '<button class="' + (g[0] !== 'random' && w.gender === g[0] ? '' : 'quiet ') + 'small-btn" ' +
          'data-act="w-gender" data-key="' + g[0] + '">' + g[1] + '</button>';
      }).join(' ') + '</div><div class="wcreate">' + ['alliance', 'horde'].map(function (f) {
        var fc = factions.filter(function (x) { return x.key === f; })[0];
        var races = raceChoices(i, f);
        return '<div class="wbanner ' + f + (fc.why ? ' closed' : '') + '"><h3>' + itemName('faction', f) + '</h3>' +
          (fc.why ? '<div class="wclosed">' + esc(fc.why) + '</div>' : '') +
          BANNER_RACES[f].map(function (key) {
            var r = races.filter(function (x) { return x.key === key; })[0];
            if (!r) return '';
            var why = fc.why || r.why;
            return '<div class="wrace' + (w.race === key ? ' sel' : '') + (why ? ' no' : '') + '" data-act="' +
              (why ? '' : 'w-race') + '" data-key="' + key + '" data-hover="race:' + key + '"><span class="icw">' +
              '<img src="' + itemIcon('race', key, w.gender) + '" alt=""></span><span class="wrace-name">' +
              RACES[key].name + '</span></div>';
          }).join('') + '</div>';
      }).join('') + '<div class="wdetail" id="wdetail">' + wizDetail() + '</div></div>';
    } else if (w.step === 'class') {
      body = '<div class="wcreate"><div class="wclasses">' + classChoices(i, w.race).map(function (c) {
        return '<div class="wclass' + (w.cls === c.key ? ' sel' : '') + (c.why ? ' no' : '') + '" data-act="' +
          (c.why ? '' : 'w-class') + '" data-key="' + c.key + '" data-hover="class:' + c.key + '"><span class="icw">' +
          '<img src="' + itemIcon('class', c.key) + '" alt=""></span><span><b>' + CLASSES[c.key].name + '</b>' +
          '<span class="muted small">' + roleSummary(c.key) + '</span></span></div>';
      }).join('') + '</div><div class="wdetail wide" id="wdetail">' + wizDetail() + '</div></div>';
    } else if (w.step === 'profs') {
      body = '<p class="muted">Gathering professions collect materials; crafting ones turn them into items. A pair ' +
        'that supplies itself (Mining + Blacksmithing) is easiest solo.</p><div class="wprofs">' +
        [1, 2].map(function (slot) {
        var mine = slot === 1 ? w.prof1 : w.prof2;
        var other = slot === 1 ? w.prof2 : w.prof1;
        return '<div><h3 class="wsub">Profession ' + slot + '</h3><div class="wlist">' +
          profChoices(i, slot, other).map(function (c) {
            var prof = Number(c.key);
            var holders = state.players.map(function (q, n) {
              if (n === i || !q.roll) return '';
              var at = q.roll.profs.indexOf(prof);
              return at >= 0 ? q.name + ' (Profession ' + (at + 1) + ')' : '';
            }).filter(Boolean);
            return wizCard('w-prof' + slot, c.key, mine === prof, c.why, '<img src="' + itemIcon('prof1', c.key) +
              '" alt=""><span><b>' + PROFESSIONS[prof].name + '</b><span class="muted small">' +
              (c.why === 'already picked' ? 'Already picked' : professionNote(prof)) + '</span>' +
              (holders.length ? '<span class="small">Has it: ' + holders.map(esc).join(', ') + '</span>' : '') +
              (c.why && c.why !== 'already picked' ? '<span class="bad small">' + esc(c.why) + '</span>' : '') +
              '</span>', PROFESSIONS[prof].name);
          }).join('') + '</div></div>';
      }).join('') + '</div>';
    } else {
      var specN = Number(w.spec.split('-')[1]);
      body = '<div class="wsummary">' +
        '<div><img src="' + itemIcon('race', w.race, w.gender) + '" alt=""><b>' + RACES[w.race].name + '</b><span>' +
        (w.gender === 'f' ? 'Female' : 'Male') + '</span></div>' +
        '<div><img src="' + itemIcon('class', w.cls) + '" alt=""><b>' + CLASSES[w.cls].name + '</b><span>' +
        itemName('faction', w.faction) + '</span></div>' +
        '<div><img src="' + itemIcon('spec', w.spec) + '" alt=""><b>' + CLASSES[w.cls].specs[specN] + '</b><span>' +
        SPEC_ROLES[w.cls][specN] + '</span></div>' + [w.prof1, w.prof2].map(function (prof) {
          return '<div><img src="' + itemIcon('prof1', String(prof)) + '" alt=""><b>' + PROFESSIONS[prof].name +
            '</b><span>' + (PROFESSIONS[prof].gather ? 'Gathering' : 'Crafting') + '</span></div>';
        }).join('') + '</div>' +
        '<label class="field" style="margin-top:14px"><span>Also use this for</span><select id="w-also">' +
        '<option value="none">Only ' + esc(p.name) + '</option>' +
        (state.players.length > 1 ? '<option value="all"' + (w.also === 'all' ? ' selected' : '') +
          '>Every player</option>' : '') +
        state.players.map(function (q, n) {
          return n === i ? '' : '<option value="' + n + '"' + (w.also === String(n) ? ' selected' : '') + '>' +
            esc(q.name) + ' too</option>';
        }).join('') + '</select></label>';
    }

    var at = wizardSteps().indexOf(w.step);
    var nav = wizardSteps().map(function (step, n) {
      var reachable = n <= at || wizardSteps().slice(0, n).every(stepReady);
      return '<button class="wstep' + (n === at ? ' on' : '') + '"' + (reachable ? ' data-act="w-go" data-key="' +
        step + '"' : ' disabled') + '>' + (n + 1) + '. ' + STEP_LABEL[step] + '</button>';
    }).join('');

    $('wizard').style.display = 'flex';
    $('wizard').innerHTML = '<div class="wizard"><div class="whead"><b>Pick a character' + (p ? ': ' + esc(p.name) :
      '') + '</b><span class="spacer"></span>' + (['race', 'class', 'profs'].indexOf(w.step) >= 0 ?
        '<button class="quiet small-btn" data-act="w-random" data-tip="Pick at random among what is open">' +
        '&#x2684; Random</button> ' : '') + '<button class="quiet small-btn" data-act="w-close" ' +
      'data-tip="Close without changing anything">&#x2715;</button></div><div class="wsteps">' + nav + '</div>' +
      (w.note ? '<p class="wnote">' + w.note + '</p>' : '') + '<div class="wbody">' + body + '</div>' +
      '<div class="wfoot"><button class="quiet" data-act="w-back"' + (at === 0 ? ' disabled' : '') + '>Back</button>' +
      '<span class="spacer"></span><a href="#prefs-title" data-act="w-advanced">Advanced preferences (chances, ' +
      'shuffle...)</a><span class="spacer"></span>' + (w.step === 'confirm' ?
        '<button data-act="w-confirm">Confirm character</button>' :
        '<button data-act="w-next"' + (stepReady(w.step) ? '' : ' disabled') + '>Next</button>') + '</div></div>';
  }

  // Why player n can't take the picked character under the rules ('' if they can)
  function pickFits(n, w) {
    var faction = factionChoices(n).filter(function (f) { return f.key === w.faction; })[0];
    if (!faction || faction.why) return faction ? faction.why : 'faction';
    var race = raceChoices(n, w.faction).filter(function (r) { return r.key === w.race; })[0];
    if (!race || race.why) return race ? race.why : 'race';
    var cls = classChoices(n, w.race).filter(function (c) { return c.key === w.cls; })[0];
    if (!cls || cls.why) return cls ? cls.why : 'class';
    var spec = specChoices(n, w.cls).filter(function (sp) { return sp.key === w.spec; })[0];
    if (!spec || spec.why) return spec ? spec.why : 'spec';
    var p1 = profChoices(n, 1, w.prof2).filter(function (c) { return Number(c.key) === w.prof1; })[0];
    var p2 = profChoices(n, 2, w.prof1).filter(function (c) { return Number(c.key) === w.prof2; })[0];
    if (!p1 || p1.why) return p1 ? p1.why : 'profession';
    if (!p2 || p2.why) return p2 ? p2.why : 'profession';
    return '';
  }

  function applyPick() {
    var w = wiz;
    var specN = Number(w.spec.split('-')[1]);
    var targets = [w.player];
    if (w.also === 'all') {
      state.players.forEach(function (q, n) { if (n !== w.player) targets.push(n); }); // the picker's own first
    }
    else if (w.also !== 'none') targets.push(Number(w.also));
    var skipped = [];
    var picked = [];
    targets.forEach(function (n) {
      var q = state.players[n];
      if (locked(q)) { skipped.push(q.name + ' (hardcore roll locked)'); return; }
      // The other players have to fit the rules too, counting the ones just given this character
      if (n !== w.player) {
        var why = pickFits(n, w);
        if (why) { skipped.push(q.name + ' (' + why + ')'); return; }
      }
      q.roll = { race: Number(w.race), cls: Number(w.cls), gender: w.gender, spec: specN, profs: [w.prof1, w.prof2],
        zone: 0 };
      picked.push(q.name);
    });
    message('Picked for ' + picked.map(esc).join(', ') + '.' +
      (skipped.length ? ' Not for: ' + skipped.map(esc).join(', ') + '.' : ''), skipped.length ? 'bad' : 'good');
    closeWizard();
  }

  function wizardClick(el) {
    var act = el.getAttribute('data-act');
    var key = el.getAttribute('data-key');
    var w = wiz;
    w.note = act === 'w-go' || act === 'w-back' || act === 'w-next' ? '' : w.note;
    if (act === 'w-close') { closeWizard(); return true; }
    if (act === 'w-advanced') {
      state.tab = Math.max(0, w.player);
      closeWizard();
      render();
      setTimeout(function () { $('prefs-title').scrollIntoView({ behavior: 'smooth' }); }, 0);
      return true;
    }
    if (act === 'w-add') {
      if (state.players.length < 40) {
        var added = newPlayer(state.players.length + 1);
        added.settings = JSON.parse(JSON.stringify(state.players[0].settings));
        state.players.push(added);
        w.player = state.players.length - 1;
        render();
      }
    } else if (act === 'w-player') {
      w.player = Number(key);
    } else if (act === 'w-draft') {
      var parts = key.split(':');
      var list = state.players[w.player].draft[parts[0]];
      var at = list.indexOf(parts[1]);
      if (at >= 0) list.splice(at, 1);
      else if (list.length < state.party.draft[parts[0]]) list.push(parts[1]);
      // Picks made before may no longer fit
      if (w.race && !raceChoices(w.player, w.faction).some(function (r) { return r.key === w.race && !r.why; })) {
        w.race = ''; w.cls = ''; w.spec = '';
      }
      if (w.cls && !classChoices(w.player, w.race || '1').some(function (c) { return c.key === w.cls && !c.why; })) {
        w.cls = ''; w.spec = '';
      }
      render();
    } else if (act === 'w-gender') {
      if (key === 'random') {
        var before = w.gender;
        w.gender = pickOne(['m', 'f']);
        var names = { m: 'Male', f: 'Female' };
        w.note = before === w.gender ? 'No change, you got the same gender (' + names[w.gender] + ').' :
          'Changed: you were ' + names[before] + ', now you\'re ' + names[w.gender] + '.';
      } else {
        w.gender = key;
      }
    } else if (act === 'w-random') {
      wizardRandom();
    } else if (act === 'w-race') {
      if (w.race !== key) { w.cls = ''; w.spec = ''; }
      w.race = key;
      w.faction = RACES[key].faction;
    } else if (act === 'w-class') {
      if (w.cls !== key) w.spec = '';
      w.cls = key;
    } else if (act === 'w-spec') {
      w.spec = key;
      w.cls = key.split('-')[0];
    } else if (act === 'w-prof1' || act === 'w-prof2') {
      var prof = Number(key);
      if (act === 'w-prof1') w.prof1 = prof; else w.prof2 = prof;
    } else if (act === 'w-go') {
      enterStep(key);
      return true;
    } else if (act === 'w-back') {
      enterStep(wizardSteps()[Math.max(0, wizardSteps().indexOf(w.step) - 1)]);
      return true;
    } else if (act === 'w-next') {
      if (stepReady(w.step)) enterStep(wizardSteps()[wizardSteps().indexOf(w.step) + 1]);
      return true;
    } else if (act === 'w-confirm') {
      var also = document.getElementById('w-also');
      w.also = also ? also.value : 'none';
      applyPick();
      render();
      return true;
    } else {
      return false;
    }
    renderWizard();
    return true;
  }

  // ---- Asking before resets and deletes ----
  // Each kind of question can be switched off with "Do not ask again" (kept in this browser); "Ask again" in the
  // top panel brings them all back.

  var NOASK_KEY = 'wr-noask';

  function noAsk() {
    try { return JSON.parse(localStorage.getItem(NOASK_KEY) || '{}') || {}; } catch (e) { return {}; }
  }

  function confirmAction(kind, title, text, okLabel, onOk) {
    if (noAsk()[kind]) { onOk(); render(); return; }
    var box = $('confirm');
    box.innerHTML = '<div class="wizard confirm"><div class="whead"><b>' + esc(title) + '</b></div>' +
      '<div class="wbody" style="min-height:0">' + text + '<label class="check" style="margin-top:14px">' +
      '<input type="checkbox" id="confirm-noask"> Do not ask again</label></div><div class="wfoot">' +
      '<button class="quiet" id="confirm-no">Cancel</button><span class="spacer"></span>' +
      '<button class="danger" id="confirm-yes">' + esc(okLabel) + '</button></div></div>';
    box.style.display = 'flex';
    function close() { box.style.display = 'none'; box.innerHTML = ''; }
    $('confirm-no').onclick = close;
    box.onclick = function (e) { if (e.target === box) close(); };
    $('confirm-yes').onclick = function () {
      if ($('confirm-noask').checked) {
        var flags = noAsk();
        flags[kind] = true;
        try { localStorage.setItem(NOASK_KEY, JSON.stringify(flags)); } catch (e) { /* storage blocked */ }
      }
      close();
      onOk();
      render();
    };
    $('confirm-yes').focus();
  }

  // ---- Events ----

  function copyText(text, done) {
    function fallback() {
      var area = document.createElement('textarea');
      area.value = text;
      document.body.appendChild(area);
      area.select();
      try { document.execCommand('copy'); done(); } catch (e) { /* nothing */ }
      document.body.removeChild(area);
    }
    if (navigator.clipboard && navigator.clipboard.writeText) {
      navigator.clipboard.writeText(text).then(done, fallback);
    } else fallback();
  }

  document.addEventListener('click', function (e) {
    var el = e.target.closest ? e.target.closest('[data-act]') : null;
    if (!el || (e.target.tagName === 'INPUT' && el.getAttribute('data-act') === 'item')) return;
    var act = el.getAttribute('data-act');
    if (act === '') return;
    if (act.indexOf('w-') === 0) {
      e.preventDefault();
      if (wiz && wizardClick(el)) return;
      return;
    }
    if (act === 'pick') { openWizard(el.hasAttribute('data-i') ? Number(el.getAttribute('data-i')) : -1); return; }
    var index = Number(el.getAttribute('data-i'));
    var cat = el.getAttribute('data-cat');
    var p = state.players[state.tab];

    if (act === 'roll') rollPlayer(index);
    else if (act === 'reroll') reroll(index, cat);
    else if (act === 'prefs' || act === 'tab') {
      state.tab = index;
      if (act === 'prefs') setTimeout(function () { $('prefs-title').scrollIntoView({ behavior: 'smooth' }); }, 0);
    } else if (act === 'copy') {
      copyText(characterCode(state.players[index]), function () {
        message(esc(state.players[index].name) + '\'s code copied.', 'good');
        renderWorld();
      });
    } else if (act === 'clear') {
      var cleared = state.players[index];
      confirmAction('clear', 'Clear ' + cleared.name + '\'s roll?', '<p>Their roll and reroll counts are cleared; ' +
        'the player and their preferences stay.</p>', 'Clear roll', function () {
        cleared.roll = null;
        cleared.counts = zeroCounts();
        cleared.decks = {};
      });
      return;
    } else if (act === 'remove') {
      if (index < 1) return; // player 1 leads the party (.coop create) and stays
      var removed = state.players[index];
      confirmAction('remove', 'Remove ' + removed.name + '?', '<p>The player, their roll, preferences and draft ' +
        'bans are deleted from this page.</p>', 'Remove', function () {
        state.players.splice(state.players.indexOf(removed), 1);
        renumberPlayers();
        state.tab = Math.min(state.tab, state.players.length - 1);
      });
      return;
    } else if (act === 'seed') {
      newWorldSeed();
    } else if (act === 'item') {
      var key = el.getAttribute('data-key');
      if (!inEra(cat, key, state.era) || blockedReason(p, cat, key)) return;
      var c = p.settings.cats[cat];
      if (c.mode === 'pick') c.pick = key;
      else c.allow[key] = !c.allow[key];
    } else if (act === 'mode') {
      var settings = p.settings.cats[cat];
      settings.mode = el.getAttribute('data-mode');
      if (settings.mode === 'pick' && !settings.pick) {
        settings.pick = KEYS[cat].filter(function (k) { return settings.allow[k] && inEra(cat, k, state.era); })[0] ||
          '';
      }
      p.decks[cat] = [];
    } else if (act === 'deck') {
      confirmAction('deck', 'Reshuffle the deck?', '<p>Every ' + CAT_LABEL[cat].toLowerCase() + ' goes back in ' +
        esc(p.name) + '\'s shuffle deck.</p>', 'Reshuffle', function () { p.decks[cat] = []; });
      return;
    } else if (act === 'all-on') {
      KEYS[cat].forEach(function (k) { p.settings.cats[cat].allow[k] = true; });
    } else if (act === 'ban') {
      var bans = state.party.ban[el.getAttribute('data-bcat')];
      var bkey = el.getAttribute('data-key');
      bans[bkey] = !bans[bkey];
    } else if (act === 'unban') {
      confirmAction('unban', 'Allow everything?', '<p>Every ban in the rules is lifted (draft bans stay).</p>',
        'Allow everything', function () { state.party.ban = { faction: {}, race: {}, cls: {}, spec: {}, prof: {} }; });
      return;
    } else if (act === 'prefs-default') {
      confirmAction('prefs-default', 'Reset ' + p.name + '\'s preferences?', '<p>Back to the default preferences ' +
        '(everything on except Death Knights, Random everywhere).</p>', 'Reset preferences', function () {
        p.settings = defaultSettings();
        p.decks = {};
      });
      return;
    } else if (act === 'prefs-default-all') {
      confirmAction('prefs-default-all', 'Reset everyone\'s preferences?', '<p>Every player\'s preferences go ' +
        'back to the defaults.</p>', 'Reset all preferences', function () {
        state.players.forEach(function (q) { q.settings = defaultSettings(); q.decks = {}; });
      });
      return;
    } else if (act === 'prefs-copy') {
      state.players.forEach(function (q) {
        if (q !== p) { q.settings = JSON.parse(JSON.stringify(p.settings)); q.decks = {}; }
      });
      message('Everyone now has ' + esc(p.name) + '\'s preferences.', 'good');
    } else return;
    render();
  });

  document.addEventListener('change', function (e) {
    var t = e.target;
    var p = state.players[state.tab];
    if (t.classList.contains('pname')) {
      state.players[Number(t.getAttribute('data-i'))].name = t.value.trim().slice(0, 24) || 'Player';
    } else if (t.classList.contains('w')) {
      p.settings.cats[t.getAttribute('data-cat')].weight[t.getAttribute('data-key')] =
        Math.max(0, Math.min(1000, Number(t.value) || 0));
    } else if (t.getAttribute('data-pref')) {
      var pref = t.getAttribute('data-pref');
      p.settings[pref] = t.type === 'checkbox' ? t.checked : t.value;
    } else return;
    render();
  });

  $('era').addEventListener('change', function () { state.era = this.value; render(); });
  $('hardcore').addEventListener('change', function () { state.hardcore = this.value === '1'; render(); });
  $('display').addEventListener('change', function () { state.display = this.value; render(); });
  $('rule-faction').addEventListener('change', function () { state.party.faction = this.value; render(); });
  [['race', 'race'], ['class', 'cls'], ['spec', 'spec'], ['prof', 'prof']].forEach(function (pair) {
    $('limit-' + pair[0]).addEventListener('change', function () {
      state.party.limit[pair[1]] = Math.max(0, Math.min(40, Math.floor(Number(this.value) || 0)));
      render();
    });
  });
  $('prof-slot').addEventListener('change', function () { state.party.profSlot = this.checked; render(); });
  $('rule-pick').addEventListener('change', function () { state.party.pick = this.value; render(); });
  DRAFT_CATS.forEach(function (k) {
    $('draft-' + k).addEventListener('change', function () {
      state.party.draft[k] = Math.max(0, Math.min(15, Math.floor(Number(this.value) || 0)));
      // Bans over the new limit go back
      state.players.forEach(function (q) { q.draft[k] = q.draft[k].slice(0, state.party.draft[k]); });
      render();
    });
  });

  // Rules as a file: the same shape as roller-config.js's rules
  function rulesForFile() {
    var r = state.party;
    var out = { faction: r.faction, limit: r.limit, profSlot: r.profSlot, compat: r.compat, pick: r.pick,
      draft: r.draft, ban: {} };
    Object.keys(r.ban).forEach(function (k) {
      out.ban[k] = Object.keys(r.ban[k]).filter(function (key) { return r.ban[k][key]; });
    });
    return out;
  }

  $('rules-save').addEventListener('click', function () {
    var blob = new Blob([JSON.stringify({ rules: rulesForFile(), era: state.era, hardcore: state.hardcore }, null, 2)],
      { type: 'application/json' });
    var link = document.createElement('a');
    link.href = URL.createObjectURL(blob);
    link.download = 'roller-rules.json';
    document.body.appendChild(link);
    link.click();
    setTimeout(function () { URL.revokeObjectURL(link.href); document.body.removeChild(link); }, 0);
  });
  $('rules-load').addEventListener('change', function () {
    var file = this.files && this.files[0];
    if (!file) return;
    var reader = new FileReader();
    reader.onload = function () {
      try {
        var data = JSON.parse(reader.result);
        var saved = CONFIG.rules;
        CONFIG.rules = data.rules || data; // read through the same path as roller-config.js
        state.party = defaultRules();
        CONFIG.rules = saved;
        if (ERAS.indexOf(data.era) >= 0) state.era = data.era;
        if (data.hardcore !== undefined) state.hardcore = !!data.hardcore;
        message('Rules loaded from ' + esc(file.name) + '.', 'good');
      } catch (e) {
        message('That file is not a rules file.');
      }
      $('rules-load').value = '';
      render();
    };
    reader.readAsText(file);
  });
  $('rules-default').addEventListener('click', function () {
    confirmAction('rules-default', 'Default rules?', '<p>The rules go back to roller-config.js, and every draft ' +
      'ban is lifted.</p>', 'Default rules', function () {
      state.party = defaultRules();
      state.players.forEach(function (q) { q.draft = emptyDraft(); });
      message('Rules back to roller-config.js.', 'good');
    });
  });
  $('compat').addEventListener('change', function () { state.party.compat = this.value; render(); });

  $('roll-all').addEventListener('click', function () { rollEveryone(); render(); });
  $('add-player').addEventListener('click', function () {
    if (state.players.length >= 40) return;
    var p = newPlayer(state.players.length + 1);
    p.settings = JSON.parse(JSON.stringify(state.players[0].settings)); // start from player 1's preferences
    state.players.push(p);
    renumberPlayers();
    render();
  });
  $('reset').addEventListener('click', function () {
    confirmAction('clear-all', 'Clear every roll?', '<p>Every player\'s roll (locked hardcore ones too) and reroll ' +
      'counts are cleared; players, names and preferences stay.</p>', 'Clear all rolls', function () {
      state.players.forEach(function (p) { p.roll = null; p.counts = zeroCounts(); p.decks = {}; });
      message('');
    });
  });

  $('reset-all').addEventListener('click', function () {
    confirmAction('reset-all', 'Reset everything?', '<p>Back to the start: one player with the default ' +
      'preferences, the rules from roller-config.js, no rolls or counts. The world seed stays. Save the page to a ' +
      'file first if you want it back later.</p>', 'Reset everything', function () {
      state = defaultState();
      closeWizard();
      message('Everything is back to the start.', 'good');
    });
  });
  $('ask-again').addEventListener('click', function (e) {
    e.preventDefault();
    try { localStorage.removeItem(NOASK_KEY); } catch (err) { /* storage blocked */ }
    message('Resets and deletes ask first again.', 'good');
    renderWorld();
  });

  // The page as a link: index.html#<party code>
  function pageLink() {
    return location.href.split('#')[0] + '#' + partyCode();
  }

  $('link-copy').addEventListener('click', function () {
    copyText(pageLink(), function () { message('Link copied.', 'good'); renderWorld(); });
  });

  // The whole page as a file: the saved state, and the world map settings
  $('page-save').addEventListener('click', function () {
    var data = { roller: 'mod-world-randomizer', v: 1, state: state, map: worldSettings() };
    var blob = new Blob([JSON.stringify(data, null, 2)], { type: 'application/json' });
    var link = document.createElement('a');
    link.href = URL.createObjectURL(blob);
    link.download = 'roller-page.json';
    document.body.appendChild(link);
    link.click();
    setTimeout(function () { URL.revokeObjectURL(link.href); document.body.removeChild(link); }, 0);
  });
  $('page-load').addEventListener('change', function () {
    var file = this.files && this.files[0];
    if (!file) return;
    var reader = new FileReader();
    reader.onload = function () {
      try {
        var data = JSON.parse(reader.result);
        if (data.roller !== 'mod-world-randomizer' || !data.state || !Array.isArray(data.state.players)) {
          throw new Error('not a page file');
        }
        try {
          localStorage.setItem(STATE_KEY, JSON.stringify(data.state));
          if (data.map) localStorage.setItem('wr-map', JSON.stringify(data.map));
        } catch (e) { /* storage blocked: loaded for this visit only */ }
        var loaded = data.state;
        loaded.party = normalizeRules(loaded.party);
        loaded.players.forEach(function (p, i) { normalizePlayer(p, i + 1); });
        if (ERAS.indexOf(loaded.era) < 0) loaded.era = 'wrath';
        if (!(loaded.tab < loaded.players.length)) loaded.tab = 0;
        state = loaded;
        message('Page loaded from ' + esc(file.name) + '.', 'good');
      } catch (e) {
        message('That file is not a saved roller page.');
      }
      $('page-load').value = '';
      render();
    };
    reader.readAsText(file);
  });

  $('party-copy').addEventListener('click', function () {
    copyText($('party-code').value, function () { message('Party code copied.', 'good'); renderWorld(); });
  });
  // An old (long) code in, the same party out as a short code
  $('convert-go').addEventListener('click', function () {
    var text = $('convert-in').value.trim();
    if (!text) return;
    var isParty = true;
    try { isParty = decodeCode(text).rules !== null || decodeCode(text).players.length > 1; } catch (e) {
      $('convert-out').value = '';
      $('convert-msg').innerHTML = '<span class="bad">That is not a World Randomizer code.</span>';
      return;
    }
    loadCodes(text, true);
    render();
    var code = isParty ? partyCode() : characterCode(state.players[0]);
    $('convert-out').value = code;
    $('convert-msg').innerHTML = '<span class="good">Converted and loaded: ' + text.length + ' &rarr; ' + code.length +
      ' characters.</span>';
  });
  $('convert-copy').addEventListener('click', function () {
    if ($('convert-out').value) copyText($('convert-out').value, function () {
      $('convert-msg').innerHTML = '<span class="good">Copied.</span>';
    });
  });

  $('codes-add').addEventListener('click', function () { loadCodes($('load-codes').value, false); render(); });
  $('codes-replace').addEventListener('click', function () {
    confirmAction('replace', 'Replace the party?', '<p>The players on this page are replaced by the ones in the ' +
      'codes.</p>', 'Replace', function () { loadCodes($('load-codes').value, true); });
  });

  ['trade-a', 'trade-b', 'trade-sa', 'trade-sb'].forEach(function (id) {
    $(id).addEventListener('change', function () { updateTradePreview(); });
  });
  $('trade-what').addEventListener('change', renderTrade);
  $('trade-go').addEventListener('click', function () {
    var result = doTrade.apply(null, tradeArgs());
    render();
    if (result.ok) updateTradePreview(result);
  });

  // The picker closes with Escape or a click outside it
  document.addEventListener('keydown', function (e) {
    if (e.key !== 'Escape') return;
    if ($('confirm').style.display === 'flex') { $('confirm').style.display = 'none'; $('confirm').innerHTML = ''; }
    else if (wiz) closeWizard();
  });
  $('wizard').addEventListener('click', function (e) { if (e.target === this) closeWizard(); });
  // Hovering a race or class shows it in the panel (only the panel is redrawn, so the hover stays)
  $('wizard').addEventListener('mouseover', function (e) {
    var el = e.target.closest ? e.target.closest('[data-hover]') : null;
    if (!wiz || !el || !$('wdetail')) return;
    var parts = el.getAttribute('data-hover').split(':');
    if (wiz.hover && wiz.hover.type === parts[0] && wiz.hover.key === parts[1]) return;
    wiz.hover = { type: parts[0], key: parts[1] };
    $('wdetail').innerHTML = wizDetail();
  });

  // Hover names
  var tip = $('tip');
  document.addEventListener('mouseover', function (e) {
    var el = e.target.closest ? e.target.closest('[data-tip]') : null;
    if (!el) { tip.style.display = 'none'; return; }
    tip.textContent = el.getAttribute('data-tip').replace(/\s+/g, ' ').replace(/ \| /g, '\n');
    tip.style.display = 'block';
  });
  document.addEventListener('mousemove', function (e) {
    if (tip.style.display !== 'block') return;
    var x = Math.min(e.clientX + 14, window.innerWidth - tip.offsetWidth - 8);
    var y = e.clientY + 18 + tip.offsetHeight > window.innerHeight ? e.clientY - tip.offsetHeight - 10 : e.clientY + 18;
    tip.style.left = Math.max(8, x) + 'px';
    tip.style.top = y + 'px';
  });

  // For tests (node): the parts that don't need the page
  window.WorldRandomizerRoller = {
    state: function () { return state; }, rollEveryone: rollEveryone, rollPlayer: rollPlayer, reroll: reroll,
    doTrade: doTrade, tradeCheck: tradeCheck, partyCovered: partyCovered, characterCode: characterCode,
    partyCode: partyCode, loadCodes: loadCodes, message: function () { return lastMessage; }, newPlayer: newPlayer,
    render: render
  };

  // Opened from a link (index.html#WR5...): load that party, rules and seed
  if (/^#WR[45]\./.test(location.hash)) {
    loadCodes(decodeURIComponent(location.hash.slice(1)), true);
    history.replaceState(null, '', location.href.split('#')[0]);
  }

  render();
})();
