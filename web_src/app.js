/*
 * morfMonitor - interface Web
 * Copyright (C) 2026 morfredus
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * Client de l'API publique de morfMonitor. Ce fichier ne connait AUCUN detail
 * d'implementation du service : il lit /api/all et /status, comme le ferait
 * RaspberryDashboard. Aucune collecte, aucune logique metier dupliquee ici.
 *
 * Regle de robustesse : la collecte de morfMonitor est portable, mais toutes
 * les metriques n'existent pas partout (CPU, memoire, charge et temperature
 * viennent de /proc et /sys, donc de Linux). Une donnee absente doit produire
 * un message explicite — jamais une case vide, jamais « 0 », qui se lirait
 * comme une mesure alors que c'est une absence de mesure.
 */

'use strict';

const REFRESH_MS = 5000;

// --- utilitaires d'affichage ------------------------------------------------

const el = (id) => document.getElementById(id);

function esc(s) {
  return String(s ?? '').replace(/[&<>"']/g, (c) => (
    { '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;', "'": '&#39;' }[c]
  ));
}

function bytes(n) {
  if (typeof n !== 'number' || !isFinite(n)) return null;
  const u = ['o', 'Kio', 'Mio', 'Gio', 'Tio'];
  let i = 0, v = n;
  while (v >= 1024 && i < u.length - 1) { v /= 1024; i++; }
  return `${v.toFixed(v < 10 && i > 0 ? 1 : 0)} ${u[i]}`;
}

function duration(sec) {
  if (typeof sec !== 'number' || sec <= 0) return null;
  const d = Math.floor(sec / 86400);
  const h = Math.floor((sec % 86400) / 3600);
  const m = Math.floor((sec % 3600) / 60);
  if (d) return `${d} j ${h} h`;
  if (h) return `${h} h ${m} min`;
  return `${m} min`;
}

function ago(sec) {
  if (typeof sec !== 'number') return '—';
  if (sec < 60) return `il y a ${Math.round(sec)} s`;
  return `il y a ${Math.round(sec / 60)} min`;
}

function row(label, value) {
  return `<div class="info-row"><span class="info-label">${esc(label)}</span>` +
         `<span class="info-value">${value ?? '—'}</span></div>`;
}

function badge(kind, text) {
  return `<span class="badge badge-${kind}">${esc(text)}</span>`;
}

function header(title, meta) {
  return `<div class="card-header"><span class="card-title">${esc(title)}</span>` +
         (meta ? `<span class="card-meta">${esc(meta)}</span>` : '') + `</div>`;
}

// Bloc d'indisponibilite : dit CE QUI manque et POURQUOI, pour qu'une metrique
// absente ne soit jamais confondue avec une metrique nulle.
function unavailable(what, why) {
  return `<div class="unavailable"><strong>${esc(what)}</strong><br>${esc(why)}</div>`;
}

function meter(percent) {
  if (typeof percent !== 'number') return '';
  const cls = percent >= 90 ? ' is-err' : percent >= 75 ? ' is-warn' : '';
  const w = Math.max(0, Math.min(100, percent));
  return `<div class="meter${cls}"><i style="width:${w}%"></i></div>`;
}

function stateBadge(state) {
  const s = String(state || '').toLowerCase();
  if (s === 'ok' || s === 'active' || s === 'running') return badge('ok', state);
  if (s === 'starting' || s === 'warning') return badge('warn', state);
  if (!s) return badge('off', 'inconnu');
  return badge('err', state);
}

// --- rendu des pages --------------------------------------------------------

function renderEtat(all, status) {
  const sys = all.system || {};

  el('c-machine').innerHTML = header('Machine') +
    row('Nom', esc(sys.hostname || '—')) +
    row('Système', esc([sys.os, sys.arch].filter(Boolean).join(' · ') || '—')) +
    row('Noyau', esc(sys.kernel || '—')) +
    row('Démarrée le', esc(sys.boot_time || '—')) +
    row('Uptime', duration(sys.uptime_s) || '—');

  el('c-sante').innerHTML = header('Service') +
    row('État', stateBadge(status.state)) +
    row('Version', esc(status.version || '—')) +
    row('Uptime service', duration(status.uptime_s) || '—') +
    row('Protocole', esc(status.proto || '—')) +
    row('Modules actifs', esc((status.metrics && status.metrics.modules) ?? '—'));

  // Apercu : la reponse courte a « est-ce que tout va bien ? ».
  const pb = problems(all);
  el('c-apercu').innerHTML = header('Aperçu', pb.length ? `${pb.length} à voir` : 'rien à signaler') +
    (pb.length
      ? pb.slice(0, 6).map((p) => row(p.what, badge(p.kind, p.state))).join('')
      : `<div class="unavailable"><strong>Aucune anomalie détectée.</strong><br>` +
        `Services supervisés en ligne, ressources sous les seuils.</div>`);
}

function renderRessources(all) {
  const r = all.resources || {};
  const parts = [];

  // CPU / memoire / charge : Linux uniquement (/proc). Absents ailleurs.
  if (r.cpu) {
    const c = r.cpu;
    parts.push(`<div class="card">${header('Processeur')}` +
      row('Utilisation', typeof c.percent === 'number' ? `${c.percent.toFixed(1)} %` : '—') +
      meter(c.percent) +
      row('Cœurs', esc(c.cores ?? '—')) +
      row('Température', typeof c.temp_c === 'number' ? `${c.temp_c.toFixed(1)} °C` : '—') +
      `</div>`);
  }
  if (r.memory) {
    const m = r.memory;
    parts.push(`<div class="card">${header('Mémoire')}` +
      row('Utilisée', `${bytes(m.used_b) ?? '—'} / ${bytes(m.total_b) ?? '—'}`) +
      meter(m.percent) +
      row('Disponible', bytes(m.available_b ?? m.free_b) ?? '—') +
      `</div>`);
  }
  if (r.load) {
    parts.push(`<div class="card">${header('Charge')}` +
      row('1 min', esc(r.load['1m'] ?? '—')) +
      row('5 min', esc(r.load['5m'] ?? '—')) +
      row('15 min', esc(r.load['15m'] ?? '—')) +
      `</div>`);
  }
  if (typeof r.processes === 'number' || (r.processes && typeof r.processes === 'object')) {
    const p = r.processes;
    parts.push(`<div class="card">${header('Processus')}` +
      row('Total', esc(typeof p === 'number' ? p : (p.total ?? '—'))) +
      (typeof p === 'object' ? row('En exécution', esc(p.running ?? '—')) : '') +
      `</div>`);
  }
  if (r.swap) {
    const s = r.swap;
    parts.push(`<div class="card">${header('Swap')}` +
      (s.total_b
        ? row('Utilisé', `${bytes(s.used_b) ?? '—'} / ${bytes(s.total_b)}`) + meter(s.percent)
        : row('Configuré', 'non')) +
      `</div>`);
  }
  if (r.disk) {
    const d = r.disk;
    parts.push(`<div class="card">${header('Stockage', d.mount || '')}` +
      row('Utilisé', `${bytes(d.used_b) ?? '—'} / ${bytes(d.total_b) ?? '—'}`) +
      meter(d.percent) +
      row('Libre', bytes(d.free_b) ?? '—') +
      `</div>`);
  }

  const missing = ['cpu', 'memory', 'load'].filter((k) => !r[k]);
  if (missing.length) {
    parts.push(`<div class="card span-all">${header('Métriques indisponibles')}` +
      unavailable(
        `Non collectées sur cette plateforme : ${missing.join(', ')}.`,
        'Ces mesures proviennent de /proc et /sys : elles ne sont renseignées que ' +
        'sous Linux, cible de production de morfMonitor. Le service reste fonctionnel ; ' +
        'seules ces valeurs manquent.') +
      `</div>`);
  }

  el('c-ressources').innerHTML = parts.join('');
}

function renderReseau(all) {
  const ifaces = (all.network && all.network.interfaces) || [];
  if (!ifaces.length) {
    el('c-interfaces').innerHTML = header('Interfaces réseau') +
      unavailable('Aucune interface remontée.', 'La collecte réseau n’a rien renvoyé.');
    return;
  }
  const rows = ifaces.map((i) => {
    const st = i.running ? badge('ok', 'active') : i.up ? badge('warn', 'montée') : badge('off', 'inactive');
    return `<tr>
      <td class="mono">${esc(i.name)}</td>
      <td>${st}</td>
      <td class="mono">${esc((i.ipv4 || []).join(', ') || '—')}</td>
      <td class="mono">${esc((i.ipv6 || []).length ? `${i.ipv6.length} adr.` : '—')}</td>
      <td class="mono">${esc(i.mac || '—')}</td>
    </tr>`;
  }).join('');

  el('c-interfaces').innerHTML = header('Interfaces réseau', `${ifaces.length} interfaces`) +
    `<div class="tbl-wrap"><table><thead><tr>
      <th class="mono">Interface</th><th>État</th><th class="mono">IPv4</th>
      <th class="mono">IPv6</th><th class="mono">MAC</th>
     </tr></thead><tbody>${rows}</tbody></table></div>`;
}

function renderServices(all) {
  const s = all.services || {};

  const units = s.systemd || [];
  el('c-systemd').innerHTML = header('Services systemd', `${units.length} supervisés`) +
    (units.length
      ? `<div class="tbl-wrap"><table><thead><tr>
           <th>Service</th><th class="mono">Unité</th><th>État</th><th class="mono">Actif</th>
         </tr></thead><tbody>` +
        units.map((u) => `<tr>
          <td>${esc(u.label || u.unit)}</td>
          <td class="mono">${esc(u.unit || '—')}</td>
          <td>${u.running ? badge('ok', 'actif') : badge('err', 'arrêté')}</td>
          <td class="mono">${esc(u.state || u.sub || '—')}</td>
        </tr>`).join('') + `</tbody></table></div>`
      : unavailable('Aucun service systemd supervisé.',
          'La liste vient de morfsystem.json (clé systemd_services). Sous Windows, ' +
          'systemd n’existe pas : cette section reste vide par construction.'));

  const probes = s.network || [];
  el('c-probes').innerHTML = header('Sondes réseau', `${probes.length} équipements`) +
    (probes.length
      ? `<div class="tbl-wrap"><table><thead><tr>
           <th>Équipement</th><th class="mono">Hôte</th><th class="mono">Port</th><th>État</th>
         </tr></thead><tbody>` +
        probes.map((p) => `<tr>
          <td>${esc(p.label || p.name)}</td>
          <td class="mono">${esc(p.host || '—')}</td>
          <td class="mono">${esc(p.port ?? '—')}</td>
          <td>${p.online ? badge('ok', 'joignable') : badge('err', 'injoignable')}</td>
        </tr>`).join('') + `</tbody></table></div>`
      : unavailable('Aucune sonde réseau déclarée.',
          s.network_grace
            ? 'Délai de grâce au démarrage en cours : les sondes ne partent qu’une fois le réseau stabilisé.'
            : 'À déclarer dans morfsystem.json (clé network_services) — un ESP32 ne répond pas à systemctl.'));
}

function renderEcosysteme(all) {
  const s = all.services || {};
  const apps = s.beacon || [];
  const offlineAfter = s.beacon_offline_after_s;

  el('c-beacon').innerHTML =
    header('Services découverts via morfBeacon', `${apps.length} annoncés`) +
    (apps.length
      ? `<div class="tbl-wrap"><table><thead><tr>
           <th>Application</th><th class="mono">Hôte</th><th class="mono">Version</th>
           <th>État</th><th class="mono">Dernier heartbeat</th><th>Déclaré</th>
         </tr></thead><tbody>` +
        apps.map((a) => `<tr>
          <td>${esc(a.label || a.app)}</td>
          <td class="mono">${esc(a.host || '—')}</td>
          <td class="mono">${esc(a.version || '—')}</td>
          <td>${a.online ? stateBadge(a.state || 'ok') : badge('err', 'hors ligne')}</td>
          <td class="mono">${esc(ago(a.last_seen_s))}</td>
          <td>${a.declared ? badge('ok', 'oui') : badge('off', 'non')}</td>
        </tr>`).join('') + `</tbody></table></div>` +
        `<div class="unavailable" style="margin-top:.8rem">` +
        `<strong>Découverte, pas configuration.</strong><br>` +
        `Ces services sont entendus sur le réseau local (diffusion UDP, protocole morfbeacon/1) : ` +
        `aucune adresse n’est connue à l’avance. « Déclaré » indique si l’application figure dans ` +
        `la liste beacon_apps de morfsystem.json. Un « non » n’est pas une anomalie : un service ` +
        `tournant sur une machine supervisée est généralement suivi par systemd, la liste beacon_apps ` +
        `étant réservée aux applications sans unité systemd (applications de bureau). ` +
        `Un service est considéré hors ligne après ${esc(offlineAfter ?? '—')} s sans annonce.</div>`
      : unavailable('Aucune annonce reçue.',
          'Aucun service morfSystem ne diffuse sur le port beacon, ou le pare-feu bloque la diffusion UDP.'));
}

// Anomalies : derivees des memes donnees, sans collecte supplementaire.
function problems(all) {
  const out = [];
  const s = all.services || {};
  const r = all.resources || {};

  (s.systemd || []).forEach((u) => {
    if (!u.running) out.push({ what: u.label || u.unit, state: 'arrêté', kind: 'err' });
  });
  (s.network || []).forEach((p) => {
    if (!p.online) out.push({ what: p.label || p.name, state: 'injoignable', kind: 'err' });
  });
  (s.beacon || []).forEach((a) => {
    if (!a.online) out.push({ what: a.label || a.app, state: 'hors ligne', kind: 'err' });
  });
  [['disk', 'Stockage'], ['memory', 'Mémoire'], ['swap', 'Swap']].forEach(([k, lbl]) => {
    const p = r[k] && r[k].percent;
    if (typeof p === 'number' && p >= 90) out.push({ what: lbl, state: `${p.toFixed(0)} %`, kind: 'err' });
    else if (typeof p === 'number' && p >= 75) out.push({ what: lbl, state: `${p.toFixed(0)} %`, kind: 'warn' });
  });
  return out;
}

function renderDiagnostic(all) {
  const pb = problems(all);
  el('c-anomalies').innerHTML =
    header('Anomalies détectées', pb.length ? `${pb.length} élément(s)` : 'aucune') +
    (pb.length
      ? pb.map((p) => row(p.what, badge(p.kind, p.state))).join('')
      : unavailable('Aucune anomalie.',
          'Tous les services supervisés répondent et aucune ressource ne dépasse 75 %.'));

  const rb = all.reboot || {};
  el('c-reboot').innerHTML = header('Dernier redémarrage') +
    row('Cause', esc(rb.cause || 'inconnue')) +
    row('Confiance', typeof rb.confidence === 'number' ? `${rb.confidence} %` : '—') +
    (rb.label ? `<div class="unavailable" style="margin-top:.6rem">${esc(rb.label)}</div>` : '') +
    (rb.evidence ? `<div class="unavailable" style="margin-top:.5rem">${esc(rb.evidence)}</div>` : '');

  const cfg = all.monitor || {};
  el('c-config').innerHTML = header('Configuration partagée') +
    row('Chargée', cfg.config_loaded ? badge('ok', 'oui') : badge('err', 'non')) +
    row('Chemin', `<span class="mono">${esc(cfg.config_path || '—')}</span>`) +
    (cfg.config_error
      ? `<div class="unavailable" style="margin-top:.6rem">${esc(cfg.config_error)}</div>`
      : '');
}

// --- boucle de rafraichissement ---------------------------------------------

function setConn(kind, text) {
  const c = el('conn-state');
  c.className = `badge badge-${kind}`;
  c.textContent = text;
}

// « Le service ne repond pas » et « le service repond mais n'a rien a dire »
// sont deux pannes differentes, avec deux causes et deux remedes differents.
// Les confondre sous un meme « injoignable » envoie chercher un probleme reseau
// la ou il s'agit d'une configuration — c'est le contraire du diagnostic.
function showServiceProblem(title, detail) {
  const block = header('Diagnostic') + unavailable(title, detail);
  ['c-machine', 'c-interfaces', 'c-systemd', 'c-beacon', 'c-anomalies'].forEach((id) => {
    const node = el(id);
    if (node) node.innerHTML = block;
  });
  ['c-sante', 'c-apercu', 'c-probes', 'c-reboot', 'c-config'].forEach((id) => {
    const node = el(id);
    if (node) node.innerHTML = '';
  });
  el('c-ressources').innerHTML = `<div class="card span-all">${block}</div>`;
}

async function refresh() {
  try {
    const [allR, statusR] = await Promise.all([fetch('/api/all'), fetch('/status')]);
    const status = statusR.ok ? await statusR.json() : {};

    // 503 : le service tourne (il a repondu), mais aucun module de supervision
    // n'est actif. Le corps porte la raison — le lire plutot que de traiter
    // tout code != 200 comme une injoignabilite.
    if (!allR.ok) {
      let apiMsg = `HTTP ${allR.status}`;
      try { apiMsg = (await allR.json()).error || apiMsg; } catch (_) { /* corps non JSON */ }

      if (statusR.ok) {
        el('hdr-version').textContent = status.version ? `v${status.version}` : '';
        el('hdr-host').textContent = status.host || '';
        setConn('warn', 'sans données');
        showServiceProblem(
          `morfMonitor répond, mais ne collecte rien — ${apiMsg}.`,
          'Le service tourne et annonce sa présence, mais aucun module de type ' +
          '« monitor » n’est actif : les routes /api/ n’ont donc rien à renvoyer. ' +
          'Vérifier la section « modules » de morfmonitor.json — seul le type ' +
          '« monitor » est reconnu. Si aucune configuration n’est trouvée, le ' +
          'service consigne la raison au démarrage (journalctl -u morfmonitor).');
      } else {
        setConn('err', 'injoignable');
        showServiceProblem(`Service injoignable — ${apiMsg}.`,
          'Ni /api/all ni /status ne répondent.');
      }
      el('foot-refresh').textContent =
        `Dernière tentative à ${new Date().toLocaleTimeString('fr-FR')}`;
      return;
    }

    const all = await allR.json();

    el('hdr-version').textContent = status.version ? `v${status.version}` : '';
    el('hdr-host').textContent = (all.system && all.system.hostname) || status.host || '';

    renderEtat(all, status);
    renderRessources(all);
    renderReseau(all);
    renderServices(all);
    renderEcosysteme(all);
    renderDiagnostic(all);

    setConn('ok', 'en ligne');
    el('foot-refresh').textContent =
      `Actualisé à ${new Date().toLocaleTimeString('fr-FR')} · toutes les ${REFRESH_MS / 1000} s`;
  } catch (e) {
    // Vraie panne de transport : le service n'a pas repondu du tout.
    setConn('err', 'injoignable');
    showServiceProblem('Service injoignable.',
      `Aucune réponse de morfMonitor (${e.message}). Le service est arrêté, ` +
      'le port est filtré, ou la machine est hors ligne.');
    el('foot-refresh').textContent = `Échec de l’actualisation : ${e.message}`;
  }
}

// --- navigation -------------------------------------------------------------

el('nav').addEventListener('click', (ev) => {
  const btn = ev.target.closest('button[data-page]');
  if (!btn) return;
  document.querySelectorAll('#nav button').forEach((b) => b.classList.toggle('active', b === btn));
  document.querySelectorAll('.page').forEach((p) => {
    p.classList.toggle('active', p.id === `page-${btn.dataset.page}`);
  });
  location.hash = btn.dataset.page;
});

const initial = location.hash.replace('#', '');
if (initial) {
  const btn = document.querySelector(`#nav button[data-page="${CSS.escape(initial)}"]`);
  if (btn) btn.click();
}

refresh();
setInterval(refresh, REFRESH_MS);
