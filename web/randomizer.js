// mod-world-randomizer: the shuffle, written to match WorldRandomizer.cpp step for step.
// The same seed, brackets, scope and zone list give the same result here and on the server.
(function (global) {
  'use strict';

  var MAX_LEVEL = 100;

  // Mulberry32, same as RandomizerRng in WorldRandomizer.h
  function Rng(seed) {
    this.state = seed >>> 0;
  }
  Rng.prototype.next = function () {
    this.state = (this.state + 0x6D2B79F5) >>> 0;
    var t = this.state;
    t = Math.imul(t ^ (t >>> 15), t | 1) >>> 0;
    t = (t ^ (t + Math.imul(t ^ (t >>> 7), t | 61))) >>> 0;
    return (t ^ (t >>> 14)) >>> 0;
  };
  Rng.prototype.below = function (count) {
    return count ? this.next() % count : 0;
  };

  // "1-10,10-20,..." -> [{min, max}], spaces ignored, bad parts skipped (same rules as the server)
  function parseBrackets(text) {
    var brackets = [];
    String(text || '').replace(/\s+/g, '').split(',').forEach(function (token) {
      if (!token) return;
      var parts = token.split('-');
      if (parts.length !== 2 || !/^\d+$/.test(parts[0]) || !/^\d+$/.test(parts[1])) return;
      var low = parseInt(parts[0], 10);
      var high = parseInt(parts[1], 10);
      if (!low || low > high || high > MAX_LEVEL) return;
      brackets.push({ min: low, max: high });
    });
    return brackets;
  }

  function homeBracket(brackets, levelMin, levelMax) {
    var mid = Math.floor((levelMin + levelMax) / 2);
    for (var i = 0; i < brackets.length; i++) {
      if (mid >= brackets[i].min && mid <= brackets[i].max) return i;
    }
    var best = 0;
    var bestDistance = Infinity;
    for (var j = 0; j < brackets.length; j++) {
      var center = Math.floor((brackets[j].min + brackets[j].max) / 2);
      var distance = Math.abs(center - mid);
      if (distance < bestDistance) {
        best = j;
        bestDistance = distance;
      }
    }
    return best;
  }

  // options: { seed, brackets, scope (0 global, 1 continent, 2 expansion), balanced }
  // Returns the enabled zones sorted by id, each with home and bracket indexes.
  function shuffleZones(allZones, options) {
    var brackets = options.brackets;
    var rng = new Rng(options.seed);
    var zones = allZones
      .filter(function (z) { return z.enabled && z.min > 0 && z.max >= z.min; })
      .map(function (z) { return Object.assign({}, z); })
      .sort(function (a, b) { return a.id - b.id; });

    var groups = {};
    zones.forEach(function (zone, index) {
      zone.home = homeBracket(brackets, zone.min, zone.max);
      zone.bracket = zone.home;
      if (!zone.shuffle) return;
      var key = 0;
      if (options.scope === 1) key = zone.continent;
      else if (options.scope === 2) key = zone.expansion;
      (groups[key] = groups[key] || []).push(index);
    });

    Object.keys(groups).map(Number).sort(function (a, b) { return a - b; }).forEach(function (key) {
      var members = groups[key];
      var pool = members.map(function (index, n) {
        return options.balanced ? n % brackets.length : zones[index].home;
      });
      for (var n = pool.length; n > 1; n--) {
        var j = rng.below(n);
        var swap = pool[n - 1];
        pool[n - 1] = pool[j];
        pool[j] = swap;
      }
      members.forEach(function (index, n) { zones[index].bracket = pool[n]; });
    });

    return zones;
  }

  function randomSeed() {
    var value = 0;
    try {
      var buffer = new Uint32Array(1);
      global.crypto.getRandomValues(buffer);
      value = buffer[0];
    } catch (e) {
      value = Math.floor(Math.random() * 4294967295);
    }
    return value || 1;
  }

  global.WorldRandomizer = {
    Rng: Rng,
    parseBrackets: parseBrackets,
    homeBracket: homeBracket,
    shuffleZones: shuffleZones,
    randomSeed: randomSeed
  };
})(window);
