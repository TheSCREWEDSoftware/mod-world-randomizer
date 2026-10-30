/*
 * Config update: an old module .conf file, brought to the module's newest .conf.dist (conf-templates.js).
 * Every setting keeps the value from the old file, new settings get their default, and settings the module no longer
 * has are kept at the end under a note. Everything happens in the browser.
 */
(function () {
  'use strict';

  var TEMPLATES = window.MODULE_CONF_TEMPLATES || {};
  var OPTION = /^\s*([A-Za-z][A-Za-z0-9_.]*)\s*=\s*(.*?)\s*$/;
  var loaded = null; // { name, text, crlf }
  var result = null; // { tpl, text, kept, changed, added, extra }

  function $(id) { return document.getElementById(id); }

  function esc(text) {
    return String(text).replace(/[&<>"']/g, function (c) {
      return { '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;', "'": '&#39;' }[c];
    });
  }

  function same(a, b) {
    var clean = function (v) { return String(v).trim().replace(/^"(.*)"$/, '$1').trim(); };
    return clean(a) === clean(b);
  }

  // Settings of a .conf text: the last line of a name wins, as in the worldserver
  function parse(text) {
    var values = {};
    var order = [];
    text.split('\n').forEach(function (line) {
      if (/^\s*#/.test(line)) return;
      var m = OPTION.exec(line);
      if (!m) return;
      if (!(m[1] in values)) order.push(m[1]);
      values[m[1]] = m[2];
    });
    return { values: values, order: order };
  }

  // The "Description:" above each setting of a template, with its continuation lines
  function descriptions(text) {
    var out = {};
    var last = '';
    var open = false;
    text.split('\n').forEach(function (line) {
      var d = /^#\s*Description:\s*(.*)$/.exec(line);
      var more = /^#\s{12,}(\S.*)$/.exec(line);
      if (d) { last = d[1]; open = true; } else if (open && more) last += ' ' + more[1];
      else open = false;
      var m = OPTION.exec(line);
      if (m && !/^\s*#/.test(line)) { out[m[1]] = last; last = ''; open = false; }
    });
    return out;
  }

  // The template whose settings the file uses most
  function guessModule(values) {
    var best = null;
    var bestCount = 0;
    Object.keys(TEMPLATES).forEach(function (name) {
      var known = parse(TEMPLATES[name].text).values;
      var count = Object.keys(values).filter(function (key) { return key in known; }).length;
      if (count > bestCount) { best = name; bestCount = count; }
    });
    return best;
  }

  function merge(tpl, userText) {
    var user = parse(userText);
    var known = {};
    var kept = [];
    var changed = [];
    var added = [];
    var lines = tpl.text.split('\n').map(function (line) {
      if (/^\s*#/.test(line)) return line;
      var m = OPTION.exec(line);
      if (!m) return line;
      var key = m[1];
      known[key] = m[2];
      if (!(key in user.values)) {
        added.push({ key: key, def: m[2] });
        return line;
      }
      kept.push(key);
      if (!same(user.values[key], m[2])) changed.push({ key: key, yours: user.values[key], def: m[2] });
      return key + ' = ' + user.values[key];
    });
    var extra = user.order.filter(function (key) { return !(key in known); });
    var text = lines.join('\n').replace(/\n+$/, '');
    if (extra.length) {
      text += '\n\n########################################\n' +
        '# Kept from your old file: these are not settings of ' + tpl.title + ' any more (removed or renamed), or\n' +
        '# belong to something else. The worldserver ignores names it doesn\'t know; delete them once checked.\n' +
        '########################################\n\n' +
        extra.map(function (key) { return key + ' = ' + user.values[key]; }).join('\n');
    }
    return { tpl: tpl, text: text + '\n', kept: kept, changed: changed, added: added, extra: extra, user: user };
  }

  function download(name, text) {
    var blob = new Blob([text], { type: 'text/plain' });
    var a = document.createElement('a');
    a.href = URL.createObjectURL(blob);
    a.download = name;
    document.body.appendChild(a);
    a.click();
    setTimeout(function () { URL.revokeObjectURL(a.href); a.remove(); }, 1000);
  }

  function message(text, kind) {
    $('conf-msg').className = 'msg' + (kind ? ' ' + kind : '');
    $('conf-msg').innerHTML = text;
  }

  function table(rows, head) {
    return '<table class="conftable"><thead><tr>' + head.map(function (h) { return '<th>' + h + '</th>'; }).join('') +
      '</tr></thead><tbody>' + rows.join('') + '</tbody></table>';
  }

  function show() {
    var r = result;
    var desc = descriptions(r.tpl.text);
    $('conf-result').style.display = '';
    $('conf-title').textContent = r.tpl.title + ': ' + loaded.name;
    $('conf-stats').innerHTML = [
      [r.kept.length, 'settings kept from your file'],
      [r.changed.length, 'of them differ from the default'],
      [r.added.length, 'new settings added with their default'],
      [r.extra.length, 'not in the module any more']
    ].map(function (s) { return '<div><b>' + s[0] + '</b><span>' + s[1] + '</span></div>'; }).join('');

    var html = '';
    if (r.changed.length) {
      html += '<h3>Your own values (kept)</h3>' + table(r.changed.map(function (c) {
        return '<tr><td><code>' + esc(c.key) + '</code></td><td class="good">' + esc(c.yours) + '</td><td>' +
          esc(c.def) + '</td></tr>';
      }), ['Setting', 'Yours', 'Default']);
    }
    if (r.added.length) {
      html += '<h3>New settings (added with their default)</h3>' + table(r.added.map(function (c) {
        return '<tr><td><code>' + esc(c.key) + '</code></td><td>' + esc(c.def) + '</td><td class="muted">' +
          esc(desc[c.key] || '') + '</td></tr>';
      }), ['Setting', 'Default', 'What it does']);
    }
    if (r.extra.length) {
      html += '<h3>Not in the module any more</h3><p class="muted small" style="margin-top:0">Kept at the end of ' +
        'the new file under a note, so nothing is lost. Check them, then delete them.</p>' +
        table(r.extra.map(function (key) {
          return '<tr><td><code>' + esc(key) + '</code></td><td>' + esc(r.user.values[key]) + '</td></tr>';
        }), ['Setting', 'Your value']);
    }
    if (!r.changed.length && !r.added.length && !r.extra.length) {
      html = '<p class="good">Your file already has every setting, all at their default. The new file only ' +
        'refreshes the descriptions.</p>';
    }
    $('conf-lists').innerHTML = html;
    $('conf-preview').textContent = r.text;
  }

  function run() {
    var name = $('conf-module').value;
    if (!loaded || !TEMPLATES[name]) return;
    result = merge(TEMPLATES[name], loaded.text);
    show();
  }

  function load(file) {
    if (!file) return;
    var reader = new FileReader();
    reader.onload = function () {
      var raw = String(reader.result);
      loaded = { name: file.name, raw: raw, text: raw.replace(/\r\n?/g, '\n'), crlf: /\r\n/.test(raw) };
      var values = parse(loaded.text).values;
      if (!Object.keys(values).length) {
        message('That file has no settings in it (lines like <code>Name = value</code>).', 'bad');
        $('conf-result').style.display = 'none';
        return;
      }
      var guess = guessModule(values);
      if (!guess) {
        message('None of these settings belong to ' + Object.keys(TEMPLATES).map(function (n) {
          return TEMPLATES[n].title;
        }).join(' or ') + '. Pick the module below if it is one of them anyway.', 'bad');
        guess = Object.keys(TEMPLATES)[0];
      } else {
        message('Read ' + esc(file.name) + ': ' + Object.keys(values).length + ' settings, ' +
          esc(TEMPLATES[guess].title) + '.', 'good');
      }
      $('conf-module').value = guess;
      run();
    };
    reader.readAsText(file);
  }

  // Setup
  $('conf-module').innerHTML = Object.keys(TEMPLATES).map(function (name) {
    return '<option value="' + name + '">' + esc(TEMPLATES[name].title) + ' (' + esc(TEMPLATES[name].file) +
      ')</option>';
  }).join('');
  $('templates-line').textContent = Object.keys(TEMPLATES).length ? 'Knows: ' + Object.keys(TEMPLATES).map(
    function (name) { return TEMPLATES[name].file; }).join(', ') : 'No templates: run tools/make_conf_templates.py';

  $('conf-module').addEventListener('change', run);
  $('conf-file').addEventListener('change', function () { load(this.files[0]); this.value = ''; });
  $('dl-old').addEventListener('click', function () {
    if (loaded) download(loaded.name.replace(/(\.conf)?(\.dist)?$/, '') + '.old.conf', loaded.raw);
  });
  $('dl-new').addEventListener('click', function () {
    if (result) download(result.tpl.file, loaded.crlf ? result.text.replace(/\n/g, '\r\n') : result.text);
  });

  var drop = $('drop');
  ['dragenter', 'dragover'].forEach(function (ev) {
    document.addEventListener(ev, function (e) { e.preventDefault(); drop.classList.add('over'); });
  });
  ['dragleave', 'drop'].forEach(function (ev) {
    document.addEventListener(ev, function (e) {
      e.preventDefault();
      if (ev === 'dragleave' && e.relatedTarget) return;
      drop.classList.remove('over');
    });
  });
  document.addEventListener('drop', function (e) {
    if (e.dataTransfer && e.dataTransfer.files.length) load(e.dataTransfer.files[0]);
  });
  drop.addEventListener('click', function (e) { if (e.target === drop) $('conf-file').click(); });

  // Tooltips (data-tip), like the other pages
  var tip = $('tip');
  document.addEventListener('mouseover', function (e) {
    var el = e.target.closest ? e.target.closest('[data-tip]') : null;
    if (!el) { tip.style.display = 'none'; return; }
    tip.textContent = el.getAttribute('data-tip').replace(/\s+/g, ' ');
    tip.style.display = 'block';
  });
  document.addEventListener('mousemove', function (e) {
    if (tip.style.display !== 'block') return;
    var x = Math.min(e.clientX + 14, window.innerWidth - tip.offsetWidth - 8);
    var y = e.clientY + 18 + tip.offsetHeight > window.innerHeight ? e.clientY - tip.offsetHeight - 10 : e.clientY + 18;
    tip.style.left = Math.max(8, x) + 'px';
    tip.style.top = y + 'px';
  });
})();
