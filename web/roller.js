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

  function zeroCounts() {
    var counts = { trade: 0 };
    COUNT_KEYS.forEach(function (key) { counts[key] = 0; });
    return counts;
  }

  function newPlayer(number) {
    return { name: 'Player ' + number, settings: defaultSettings(), roll: null, counts: zeroCounts(), decks: {} };
  }

  function defaultState() {
    return {
      v: 3, era: 'wrath', hardcore: true, display: 'icons', tab: 0,
      party: { faction: 'same', dupRace: true, dupClass: false, dupSpec: true, profDup: 'yes', compat: 'off' },
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
    var fresh = defaultState();
    state.party = Object.assign(fresh.party, state.party || {});
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

  function isAllowed(p, cat, key) {
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
    state.players.forEach(function (q, j) {
      if (j === index || !q.roll) return;
      var faction = RACES[q.roll.race].faction;
      if (rules.faction === 'same' && !ctx.faction) ctx.faction = faction;
      if (rules.faction === 'different') ctx.notFaction[faction] = true;
      if (!rules.dupRace) ctx.race[q.roll.race] = true;
      if (!rules.dupClass) ctx.cls[q.roll.cls] = true;
      if (!rules.dupSpec && q.roll.spec >= 0) ctx.spec[q.roll.cls + '-' + q.roll.spec] = true;
      q.roll.profs.forEach(function (prof, slot) {
        if (prof < 0) return;
        if (rules.profDup === 'slot') ctx['prof' + (slot + 1)][prof] = true;
        if (rules.profDup === 'no') ctx.profAny[prof] = true;
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
  }

  function readPlayer(r, index) {
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
      w.flag(rules.dupRace);
      w.flag(rules.dupClass);
      w.flag(rules.dupSpec);
      w.put(['yes', 'slot', 'no'].indexOf(rules.profDup), 2);
      w.put(['off', 'player', 'party'].indexOf(rules.compat), 2);
      w.put(players.length - 1, 6);
    }
    players.forEach(function (p) { writePlayer(w, p, party ? state.players.indexOf(p) : 0); });
    return 'WR4.' + w.toString();
  }

  function readPackedCode(text) {
    var r = new BitReader(text);
    var party = r.flag();
    var out = { era: ERAS[r.get(2)] || 'wrath', hardcore: r.flag(), rules: null, players: [] };
    out.world = readWorld(r);
    var count = 1;
    if (party) {
      out.rules = {
        faction: ['same', 'free', 'different'][r.get(2)] || 'same', dupRace: r.flag(), dupClass: r.flag(),
        dupSpec: r.flag(), profDup: ['yes', 'slot', 'no'][r.get(2)] || 'yes',
        compat: ['off', 'player', 'party'][r.get(2)] || 'off'
      };
      count = r.get(6) + 1;
    }
    for (var i = 0; i < count; i++) out.players.push(readPlayer(r, i));
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
    return {
      faction: ['same', 'free', 'different'][data[0]] || 'same', dupRace: !!data[1], dupClass: !!data[2],
      dupSpec: !!data[3], profDup: ['yes', 'slot', 'no'][data[4]] || 'yes',
      compat: ['off', 'player', 'party'][data[5]] || 'off'
    };
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
    if (/^WR4\./.test(code)) return readPackedCode(code.slice(4));
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
      ((showNames() || !iconHtml) ? '<div class="tname">' + (name || '&mdash;') + '</div>' : '') +
      (sub ? '<div class="sub">' + sub + '</div>' : '') +
      (canReroll ? '<button class="reroll" data-act="reroll" data-cat="' + cat + '" data-i="' + index +
        '" data-tip="Reroll ' + label.toLowerCase() + ' (' + count + ' so far)">&#x21bb; ' + count + '</button>' : '') +
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
      '<button class="quiet" data-act="prefs" data-i="' + index + '" data-tip="Preferences">&#x2699;</button>' +
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
        r.spec >= 0 ? cls.specs[r.spec] : '', r.spec >= 0 ? '' : 'not rolled', can && p.settings.rollSpec) +
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

  function prefItem(p, cat, key, pct) {
    var s = p.settings.cats[cat];
    var era = inEra(cat, key, state.era);
    var off = s.mode === 'pick' ? s.pick !== key : !s.allow[key];
    var name = itemName(cat, key);
    var tip = name + (era ? '' : ' (' + ERA_NAME[since(cat, key)] + ')') +
      (s.mode === 'chances' && !off && era ? ': ' + (pct[key] || 0) + '%' : '') +
      (s.mode === 'shuffle' && !off && era ? (deckOf(p, cat).indexOf(key) >= 0 ? ': still in the deck' :
        ': already drawn') : '');
    var shortName = cat === 'spec' ? CLASSES[key.split('-')[0]].specs[key.split('-')[1]] : name;
    var gender = p.settings.gender === 'f' ? 'f' : 'm';
    return '<div class="item' + (off ? ' off' : '') + (era ? '' : ' era') +
      (s.mode === 'pick' && !off ? ' picked' : '') +
      (s.mode === 'shuffle' && !off && era && deckOf(p, cat).indexOf(key) < 0 ? ' drawn' : '') +
      '" data-act="item" data-cat="' + cat + '" data-key="' + key + '" data-tip="' + esc(tip) + '">' +
      (showIcons() ? '<img src="' + itemIcon(cat, key, gender) + '" alt="' + esc(name) + '">' : '') +
      (showNames() ? '<span class="iname">' + esc(shortName) + '</span>' : '') +
      (s.mode === 'chances' ? '<input type="number" class="w" min="0" max="1000" data-cat="' + cat + '" data-key="' +
        key + '" value="' + (Number(s.weight[key]) || 0) + '" aria-label="' + esc(name) + ' chance">' +
        '<span class="pct">' + (off || !era ? '&ndash;' : (pct[key] || 0) + '%') + '</span>' : '') +
      '</div>';
  }

  function renderPrefs() {
    var index = state.tab;
    var p = state.players[index];
    $('prefs-title').textContent = 'Preferences: ' + p.name;
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

    html += '<p style="margin:12px 0 0"><button class="quiet" data-act="prefs-default">Back to default</button> ' +
      (state.players.length > 1 ? '<button class="quiet" data-act="prefs-copy">Use these for every player</button>' :
        '') + '</p>';
    $('prefs').innerHTML = html;
  }

  function renderSetup() {
    $('era').value = state.era;
    $('hardcore').value = state.hardcore ? '1' : '0';
    $('display').value = state.display;
    $('rule-faction').value = state.party.faction;
    $('dup-race').checked = state.party.dupRace;
    $('dup-class').checked = state.party.dupClass;
    $('dup-spec').checked = state.party.dupSpec;
    $('dup-prof').value = state.party.profDup;
    $('compat').value = state.party.compat;
    $('roll-all').textContent = state.players.length > 1 ? 'Roll everyone' : 'Roll';
  }

  function render() {
    renderSetup();
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
      cleared.roll = null;
      cleared.counts = zeroCounts();
      cleared.decks = {};
    } else if (act === 'remove') {
      if (index < 1) return; // player 1 leads the party (.coop create) and stays
      state.players.splice(index, 1);
      renumberPlayers();
      state.tab = Math.min(state.tab, state.players.length - 1);
    } else if (act === 'seed') {
      newWorldSeed();
    } else if (act === 'item') {
      var key = el.getAttribute('data-key');
      if (!inEra(cat, key, state.era)) return;
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
      p.decks[cat] = [];
    } else if (act === 'all-on') {
      KEYS[cat].forEach(function (k) { p.settings.cats[cat].allow[k] = true; });
    } else if (act === 'prefs-default') {
      p.settings = defaultSettings();
      p.decks = {};
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
  ['race', 'class', 'spec'].forEach(function (what) {
    $('dup-' + what).addEventListener('change', function () {
      state.party['dup' + what.charAt(0).toUpperCase() + what.slice(1)] = this.checked;
      render();
    });
  });
  $('dup-prof').addEventListener('change', function () { state.party.profDup = this.value; render(); });
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
    state.players.forEach(function (p) { p.roll = null; p.counts = zeroCounts(); p.decks = {}; });
    message('');
    render();
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
  $('codes-replace').addEventListener('click', function () { loadCodes($('load-codes').value, true); render(); });

  ['trade-a', 'trade-b', 'trade-sa', 'trade-sb'].forEach(function (id) {
    $(id).addEventListener('change', function () { updateTradePreview(); });
  });
  $('trade-what').addEventListener('change', renderTrade);
  $('trade-go').addEventListener('click', function () {
    var result = doTrade.apply(null, tradeArgs());
    render();
    if (result.ok) updateTradePreview(result);
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

  render();
})();
