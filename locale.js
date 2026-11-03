(function (catalogs) {
  'use strict';

  const languages = { it: 'Italiano' };
  for (const [code, catalog] of Object.entries(catalogs)) languages[code] = catalog.name;
  const supported = code => Object.prototype.hasOwnProperty.call(languages, code);
  const validChoice = code => code === 'auto' || supported(code);
  const key = 'qc5-language';
  let stored;
  try { stored = localStorage.getItem(key); } catch (_) {}
  const requested = new URL(location.href).searchParams.get('lang');
  const choice = validChoice(requested) ? requested : validChoice(stored) ? stored : 'auto';
  const browserLanguage = (navigator.languages || [navigator.language])
    .map(tag => (tag || '').toLowerCase().split('-')[0]).find(supported);
  const language = choice === 'auto' ? browserLanguage || 'en' : choice;
  const messages = catalogs[language] ? catalogs[language].strings : {};

  function text(message, values) {
    if (typeof message !== 'string') return message;
    const translated = Object.prototype.hasOwnProperty.call(messages, message)
      ? messages[message] : message;
    return values ? translated.replace(/\{(\w+)\}/g, (match, name) =>
      Object.prototype.hasOwnProperty.call(values, name) ? String(values[name]) : match)
      : translated;
  }

  function url(path, preference = choice) {
    const target = new URL(path, location.href);
    target.searchParams.set('lang', preference);
    return target.href;
  }

  window.qc5Text = text;
  window.qc5Locale = { language, url };
  document.documentElement.lang = language;
  document.title = text(document.title);

  // Initial markup contains only bundled labels. Runtime values such as SSIDs,
  // file names and camera names are assigned separately and remain literal.
  const walker = document.createTreeWalker(document.body, NodeFilter.SHOW_TEXT);
  for (let node = walker.nextNode(); node; node = walker.nextNode()) {
    if (node.parentElement.closest('script, style')) continue;
    const value = node.nodeValue;
    const trimmed = value.trim();
    if (!trimmed) continue;
    const label = trimmed.replace(/\s+/g, ' ');
    const translated = text(label);
    if (translated !== label) node.nodeValue = value.replace(trimmed, () => translated);
  }
  for (const el of document.querySelectorAll('[title], [aria-label], [placeholder]')) {
    for (const attr of ['title', 'aria-label', 'placeholder']) {
      if (el.hasAttribute(attr)) el.setAttribute(attr, text(el.getAttribute(attr)));
    }
  }
  for (const link of document.querySelectorAll('[data-qc5-locale-link]')) link.href = url(link.href);

  const control = document.getElementById('qc5-language-control');
  if (!control) return;
  const select = document.createElement('select');
  select.setAttribute('aria-label', text('Lingua'));
  for (const [value, name] of Object.entries({ auto: text('Automatico'), ...languages })) {
    const option = new Option(name, value);
    select.add(option);
  }
  select.value = choice;
  select.addEventListener('change', () => {
    try { localStorage.setItem(key, select.value); } catch (_) {}
    location.replace(url(location.href, select.value));
  });
  control.append(select);
})(/* QC5_LOCALE_CATALOGS */);
