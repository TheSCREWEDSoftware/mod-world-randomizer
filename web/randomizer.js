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
    // Lowest first, whatever order they were typed in (stable, like std::stable_sort)
    return brackets
      .map(function (b, i) { return { b: b, i: i }; })
      .sort(function (x, y) { return (x.b.min - y.b.min) || (x.b.max - y.b.max) || (x.i - y.i); })
      .map(function (x) { return x.b; });
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

  // Brackets with a gap between them (some levels have no zone): [[prev, next], ...]
  function bracketGaps(brackets) {
    var gaps = [];
    for (var i = 1; i < brackets.length; i++) {
      if (brackets[i].min > brackets[i - 1].max + 1) gaps.push([brackets[i - 1], brackets[i]]);
    }
    return gaps;
  }

  // options: { seed, brackets, scope (0 global, 1 continent, 2 expansion), balanced, adjacent, factionStarts }
  // Returns the enabled zones sorted by id, each with home and bracket indexes.
  function shuffleZones(allZones, options) {
    var brackets = options.brackets;
    var rng = new Rng(options.seed);
    var zones = allZones
      .filter(function (z) { return z.enabled && z.min > 0 && z.max >= z.min; })
      .map(function (z) { return Object.assign({}, z); })
      .sort(function (a, b) { return a.id - b.id; });

    var indexById = {};
    zones.forEach(function (zone, index) { indexById[zone.id] = index; });

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
      if (!options.adjacent) {
        for (var n = pool.length; n > 1; n--) {
          var j = rng.below(n);
          var swap = pool[n - 1];
          pool[n - 1] = pool[j];
          pool[j] = swap;
        }
        members.forEach(function (index, n) { zones[index].bracket = pool[n]; });
        return;
      }

      // Adjacent: grow outwards from a random zone through bordering zones, lowest bracket first
      pool.sort(function (a, b) { return a - b; });
      var assigned = {};
      for (var k = 0; k < pool.length; k++) {
        var candidates = [];
        if (k) {
          members.forEach(function (index) {
            if (assigned[index]) return;
            var borders = (zones[index].n || []).some(function (neighbour) {
              var other = indexById[neighbour];
              return other !== undefined && assigned[other];
            });
            if (borders) candidates.push(index);
          });
        }
        if (!candidates.length) {
          members.forEach(function (index) { if (!assigned[index]) candidates.push(index); });
        }
        var chosen = candidates[rng.below(candidates.length)];
        assigned[chosen] = true;
        zones[chosen].bracket = pool[k];
      }
    });

    if (options.factionStarts) ensureFactionStarts(zones, options.scope);
    return zones;
  }

  function groupKey(zone, scope) {
    if (scope === 1) return zone.continent;
    if (scope === 2) return zone.expansion;
    return 0;
  }

  // Same as WorldRandomizerMgr::EnsureFactionStarts: each faction gets one of its own zones in bracket 0
  function ensureFactionStarts(zones, scope) {
    [0, 1].forEach(function (team) {
      var present = zones.some(function (z) { return z.bracket === 0 && z.territory === team; });
      if (present) return;
      var own = null;
      zones.forEach(function (z) {
        if (z.shuffle && z.territory === team && (!own || z.bracket < own.bracket)) own = z;
      });
      if (!own) return;
      for (var i = 0; i < zones.length; i++) {
        var z = zones[i];
        if (z.shuffle && z.bracket === 0 && z.territory === 2 && groupKey(z, scope) === groupKey(own, scope)) {
          var swap = z.bracket;
          z.bracket = own.bracket;
          own.bracket = swap;
          break;
        }
      }
    });
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
    bracketGaps: bracketGaps,
    randomSeed: randomSeed
  };
})(window);
