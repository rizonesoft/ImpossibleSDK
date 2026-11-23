const $ = s => document.querySelector(s), $$ = s => [...document.querySelectorAll(s)];
const base = document.documentElement.dataset.base;

/* Light and dark: the system's choice until the reader makes one. */
$('#theme').onclick = () => {
  const root = document.documentElement;
  const dark = root.dataset.theme ? root.dataset.theme === 'dark' : matchMedia('(prefers-color-scheme: dark)').matches;
  root.dataset.theme = dark ? 'light' : 'dark';
  try { localStorage.setItem('im-theme', root.dataset.theme); } catch { /* it lasts for this page */ }
};

$$('.copy').forEach(button => button.onclick = async () => {
  const code = button.closest('.code').querySelector('code');
  try {
    await navigator.clipboard.writeText(code.textContent);
    button.textContent = 'Copied';
  } catch {
    getSelection().selectAllChildren(code);
    button.textContent = 'Selected';
  }
  setTimeout(() => button.textContent = 'Copy', 1500);
});

/* Search: every word has to be in the page's title, a heading or the text below it. */
const box = $('#q'), list = $('#results');
let entries, shown = [], at = -1;
let asked = 0;
const close = () => { asked++; list.hidden = true; shown = []; box.setAttribute('aria-expanded', 'false'); box.removeAttribute('aria-activedescendant'); at = -1; };
const pick = n => {
  at = n;
  shown.forEach((o, i) => o.setAttribute('aria-selected', i === n));
  if (n >= 0) { box.setAttribute('aria-activedescendant', shown[n].id); shown[n].scrollIntoView({block: 'nearest'}); }
};
async function find() {
  const words = box.value.toLowerCase().split(/\s+/).filter(Boolean);
  if (!words.length) return close();
  const mine = ++asked;
  try { entries ??= await (await fetch(base + 'docs/search.json')).json(); } catch { entries = undefined; }
  if (mine !== asked) return; /* a newer search, or the list was closed */
  const score = e => words.reduce((sum, w) => {
    const h = e.h.toLowerCase(), t = e.t.toLowerCase(), x = e.x.toLowerCase();
    const s = h.startsWith(w) ? 9 : h.includes(w) ? 6 : t.includes(w) ? 4 : x.includes(w) ? 1 : 0;
    return s && sum >= 0 ? sum + s : -1;
  }, 0);
  const hits = (entries ?? []).map(e => [score(e), e]).filter(([s]) => s > 0).sort((a, b) => b[0] - a[0]).slice(0, 12);
  list.replaceChildren();
  shown = hits.map(([, e], i) => {
    const o = document.createElement('li'), small = document.createElement('small');
    o.id = 'r' + i; o.setAttribute('role', 'option'); o.setAttribute('aria-selected', 'false'); o.dataset.url = base + 'docs/' + e.u;
    o.textContent = e.h ? e.h + ' · ' + e.t : e.t;
    small.textContent = e.x;
    o.append(small);
    o.onclick = () => location.href = o.dataset.url;
    list.append(o);
    return o;
  });
  if (!hits.length) {
    const none = document.createElement('li');
    none.className = 'none'; none.setAttribute('role', 'option'); none.setAttribute('aria-disabled', 'true');
    none.textContent = entries ? 'Nothing found. Try one word, such as a function name.' : 'The search list could not be loaded. Reload the page to try again.';
    list.append(none);
  }
  list.hidden = false;
  box.removeAttribute('aria-activedescendant');
  box.setAttribute('aria-expanded', 'true');
  at = -1;
}
box.oninput = find;
box.onkeydown = e => {
  if (e.key === 'ArrowDown' && shown.length) { e.preventDefault(); pick((at + 1) % shown.length); }
  else if (e.key === 'ArrowUp' && shown.length) { e.preventDefault(); pick((at + shown.length) % shown.length - (at < 1 ? 0 : 1)); }
  else if (e.key === 'Enter' && shown.length && !list.hidden) location.href = shown[Math.max(at, 0)].dataset.url;
  else if (e.key === 'Escape') { close(); }
};
addEventListener('click', e => { if (!e.target.closest('.search')) close(); });
addEventListener('keydown', e => {
  if (e.key === '/' && !e.ctrlKey && !e.metaKey && !e.altKey && !/INPUT|TEXTAREA/.test(document.activeElement.tagName)) { e.preventDefault(); box.focus(); }
});

/* The first page: the two files, the case of tiers, and the comparison. */
const tabs = $$('.tabs button');
const tab = chosen => tabs.forEach(b => {
  const on = b === chosen;
  b.setAttribute('aria-selected', on);
  b.tabIndex = on ? 0 : -1;
  document.getElementById(b.getAttribute('aria-controls')).hidden = !on;
});
tabs.forEach((b, i) => {
  b.onclick = () => tab(b);
  b.onkeydown = e => {
    if (e.key !== 'ArrowRight' && e.key !== 'ArrowLeft') return;
    const next = tabs[(i + (e.key === 'ArrowRight' ? 1 : tabs.length - 1)) % tabs.length];
    tab(next);
    next.focus();
  };
});
if ($('#tiers')) {
  const tiers = JSON.parse($('#tiers').textContent), drawers = $$('.drawer');
  const open = n => {
    const all = tiers.slice(0, n + 1).reduce((sum, t) => sum + Object.values(t.f).flat().length, 0), chips = $('#tf');
    drawers.forEach((d, i) => { d.classList.toggle('in', i <= n); d.setAttribute('aria-pressed', i === n); });
    $('#tn').textContent = 'Tier ' + n + ': ' + tiers[n].n;
    $('#td').textContent = all + ' functions in all. This drawer adds:';
    chips.replaceChildren();
    for (const [dll, names] of Object.entries(tiers[n].f)) {
      const title = document.createElement('b');
      title.textContent = dll;
      chips.append(title, ...names.map(name => Object.assign(document.createElement('code'), {textContent: name})));
    }
    $('#tc').textContent = 'impossible_add_program(Hello ' + (n ? 'WINDOW' : 'CONSOLE') + ' TIER ' + n + ' SOURCES src/main.c)';
  };
  drawers.forEach((d, i) => d.onclick = () => open(i));
  open(1);
  $('#cmp').oninput = e => $('.cmp').style.setProperty('--x', e.target.value + '%');
}
