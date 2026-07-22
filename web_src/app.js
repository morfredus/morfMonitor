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

// Etat d'une unite systemd. Le backend renvoie l'ActiveState de systemd tel
// quel (active, inactive, failed, activating, disabled, unknown) ET un booleen
// `active`. Rendre CET etat, plutot que d'inventer un booleen : afficher un
// « arrete » deduit d'un champ inexistant a cote de la colonne qui affichait
// « active » produisait une contradiction dans la meme ligne.
function systemdBadge(u) {
  const s = String(u.state || '').toLowerCase();
  if (s === 'active')   return badge('ok',   'actif');
  if (s === 'failed')   return badge('err',  'échec');
  if (s === 'inactive') return badge('err',  'arrêté');
  if (s === 'disabled') return badge('off',  'désactivé');
  if (s === 'activating' || s === 'deactivating' || s === 'reloading')
    return badge('warn', s);
  return badge('off', s || 'inconnu');
}

// Etat d'une sonde reseau. Le backend distingue quatre cas et prend soin de ne
// PAS confondre « pas encore sonde » et « hors ligne » (delai de grace mDNS au
// demarrage). Les ecraser en un booleen annulait cette precaution et affichait
// « injoignable » pour un equipement parfaitement joignable.
function probeBadge(p) {
  const s = String(p.state || '').toLowerCase();
  if (s === 'online')   return badge('ok',   'joignable');
  if (s === 'offline')  return badge('err',  'injoignable');
  if (s === 'pending')  return badge('warn', 'en attente');
  if (s === 'disabled') return badge('off',  'désactivé');
  return badge('off', s || 'inconnu');
}

// Lien vers l'interface d'un service decouvert.
//
// C'est un `href` ordinaire vers l'adresse propre du service : le navigateur s'y
// rend directement. morfMonitor n'est PAS sur le chemin de la requete -- il ne
// relaie rien, n'ouvre aucune session, n'authentifie personne. C'est ce qui le
// garde observatoire et non portail : coupez-le, ces adresses restent
// joignables, seule la commodite de les trouver disparait.
//
// rel="noopener" : une page ouverte depuis ici ne doit pas pouvoir manipuler
// celle-ci via window.opener.
function webUiLink(a) {
  const ui = a.web_ui;
  if (!ui || !ui.url) {
    // Distinguer « annonce une interface mais son detail n'a pu etre lu » de
    // « n'en annonce aucune » : la premiere est un incident, la seconde un fait.
    const declares = Array.isArray(a.capabilities) && a.capabilities.includes('web_ui');
    return declares ? badge('warn', 'annoncée, injoignable') : '<span class="info-label">—</span>';
  }
  const label = esc(ui.label || serviceName(a.app));
  const title = ui.description ? ` title="${esc(ui.description)}"` : '';
  return `<a href="${esc(ui.url)}" target="_blank" rel="noopener"${title}>${label} ↗</a>`;
}

// Nom d'affichage d'un service : on part du nom qu'il ANNONCE (champ `app` du
// heartbeat), jamais d'un libellé défini ici. Un service renommé s'affiche
// alors correctement de lui-même, sans qu'on touche à morfMonitor — la config
// n'est plus une seconde source de vérité qui peut mentir.
//
// Seul le préfixe « morf » est normalisé : minuscule, et la lettre suivante en
// majuscule, pour que « morfdashboard » et « morfDashboard » se lisent pareil.
// Les majuscules internes sont celles du service et sont conservées, car on ne
// peut pas les deviner : « morfTemplateService » reste tel quel. Un nom sans
// préfixe morf (ComponentHub, MeteoHub) est affiché exactement comme annoncé.
function serviceName(app) {
  if (typeof app !== 'string' || !app) return '—';
  if (/^morf/i.test(app)) {
    const rest = app.slice(4);
    return 'morf' + (rest ? rest.charAt(0).toUpperCase() + rest.slice(1) : '');
  }
  return app;
}

function stateBadge(state) {
  const s = String(state || '').toLowerCase();
  if (s === 'ok' || s === 'active' || s === 'running') return badge('ok', state);
  if (s === 'starting' || s === 'warning') return badge('warn', state);
  if (!s) return badge('off', 'inconnu');
  return badge('err', state);
}

// --- rendu des pages --------------------------------------------------------

// Adresses IPv4 des interfaces REELLEMENT actives, sous la forme
// « 192.168.1.105 (wlan0) ». L'adresse ne vivait que dans l'onglet Réseau ;
// or c'est la premiere chose qu'on cherche quand un client externe — SSH, un
// client FTP, un signet — cesse de se connecter apres un changement de bail
// DHCP. La faire chercher dans un second onglet transforme une question de
// trois secondes en enquete.
function primaryAddresses(all) {
  const ifaces = (all.network && all.network.interfaces) || [];
  const live = ifaces.filter((i) => i.running && (i.ipv4 || []).length);
  if (!live.length) return null;
  return live.map((i) => `${esc(i.ipv4[0])} <span class="info-label">(${esc(i.name)})</span>`)
             .join('<br>');
}

function renderEtat(all, status) {
  const sys = all.system || {};
  const addr = primaryAddresses(all);

  el('c-machine').innerHTML = header('Machine') +
    row('Nom', esc(sys.hostname || '—')) +
    row('Adresse', addr || '<span class="info-label">aucune interface active</span>') +
    (sys.model ? row('Modèle', esc(sys.model)) : '') +
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

// Schema reel de /api/resources (voir HostCollectors.cpp) :
//   cpu_percent, cpu_freq_mhz   -- A PLAT, pas dans un objet « cpu »
//   load                        -- TABLEAU [1 min, 5 min, 15 min]
//   memory, swap, disk          -- objets { total_b, used_b, free_b, percent }
//   temperature { cpu_c, gpu_c }
//   throttling { raw, undervoltage_now, throttled_now, *_since_boot }
function renderRessources(all) {
  const r = all.resources || {};
  const parts = [];

  const temp = r.temperature || {};
  if (typeof r.cpu_percent === 'number' || typeof r.cpu_freq_mhz === 'number') {
    parts.push(`<div class="card">${header('Processeur')}` +
      row('Utilisation', typeof r.cpu_percent === 'number' ? `${r.cpu_percent.toFixed(1)} %` : '—') +
      meter(r.cpu_percent) +
      row('Fréquence', typeof r.cpu_freq_mhz === 'number' ? `${r.cpu_freq_mhz} MHz` : '—') +
      row('Température CPU', typeof temp.cpu_c === 'number' ? `${temp.cpu_c.toFixed(1)} °C` : '—') +
      (typeof temp.gpu_c === 'number' ? row('Température GPU', `${temp.gpu_c.toFixed(1)} °C`) : '') +
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

  // load est un tableau : le lire comme un objet {1m,5m,15m} affichait trois
  // tirets sur une machine dont la charge etait parfaitement mesuree.
  if (Array.isArray(r.load) && r.load.length >= 3) {
    parts.push(`<div class="card">${header('Charge moyenne')}` +
      row('1 min', r.load[0].toFixed(2)) +
      row('5 min', r.load[1].toFixed(2)) +
      row('15 min', r.load[2].toFixed(2)) +
      `</div>`);
  }

  if (r.swap) {
    const s = r.swap;
    parts.push(`<div class="card">${header('Swap')}` +
      (s.total_b
        ? row('Utilisé', `${bytes(s.used_b) ?? '—'} / ${bytes(s.total_b)}`) + meter(s.percent) +
          row('Libre', bytes(s.free_b) ?? '—')
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

  // Bridage : sous-tension et limite thermique. Le collecteur le dit lui-meme,
  // « c'est le diagnostic le plus utile d'un Pi instable, et il n'apparait
  // nulle part ailleurs ». Un Pi sous-alimente corrompt sa carte SD et fige des
  // services sans qu'aucun journal ne l'explique : cette section merite d'etre
  // lue meme quand tout va bien.
  if (r.throttling) {
    const t = r.throttling;
    const flag = (now, since, labelNow, labelSince) =>
      now   ? badge('err',  labelNow)
      : since ? badge('warn', labelSince)
      : badge('ok', 'non');
    const clean = !t.undervoltage_now && !t.throttled_now &&
                  !t.undervoltage_since_boot && !t.throttled_since_boot;
    parts.push(`<div class="card">${header('Alimentation et bridage',
                                           clean ? 'sain' : 'à surveiller')}` +
      row('Sous-tension', flag(t.undervoltage_now, t.undervoltage_since_boot,
                               'maintenant', 'depuis le démarrage')) +
      row('Bridage thermique', flag(t.throttled_now, t.throttled_since_boot,
                                    'maintenant', 'depuis le démarrage')) +
      (clean ? '' :
        `<div class="unavailable" style="margin-top:.6rem">` +
        `Une sous-tension corrompt la carte SD et fige des services sans laisser ` +
        `de trace dans les journaux. Vérifier l’alimentation et le câble avant ` +
        `de chercher ailleurs.</div>`) +
      `</div>`);
  }

  const expected = { cpu_percent: 'processeur', memory: 'mémoire', load: 'charge' };
  const missing = Object.keys(expected).filter((k) => r[k] === undefined);
  if (missing.length) {
    parts.push(`<div class="card span-all">${header('Métriques indisponibles')}` +
      unavailable(
        `Non collectées sur cette plateforme : ${missing.map((k) => expected[k]).join(', ')}.`,
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
    // up sans running = interface administrativement montee mais sans porteuse
    // (cable debranche, WiFi non associe). « montee » etait exact mais opaque.
    const st = i.running ? badge('ok', 'active')
             : i.up     ? badge('warn', 'sans lien')
                        : badge('off', 'inactive');
    // Cette page est celle du detail : lister les adresses plutot que de les
    // masquer derriere un compteur. Au-dela de deux, on resume pour ne pas
    // etirer la ligne.
    const v6 = i.ipv6 || [];
    const v6txt = v6.length === 0 ? '—'
                : v6.length <= 2  ? v6.join(', ')
                                  : `${v6.slice(0, 2).join(', ')} +${v6.length - 2}`;
    return `<tr>
      <td class="mono">${esc(i.name)}</td>
      <td>${st}</td>
      <td class="mono">${esc((i.ipv4 || []).join(', ') || '—')}</td>
      <td class="mono">${esc(v6txt)}</td>
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
           <th>Service</th><th class="mono">Unité</th><th>État</th><th class="mono">Détail</th>
         </tr></thead><tbody>` +
        units.map((u) => `<tr>
          <td>${esc(u.label || u.unit)}</td>
          <td class="mono">${esc(u.unit || '—')}</td>
          <td>${systemdBadge(u)}</td>
          <td class="mono">${esc(u.sub_state || u.state || '—')}</td>
        </tr>`).join('') + `</tbody></table></div>`
      : unavailable('Aucun service systemd supervisé.',
          'La liste vient de morfsystem.json (clé systemd_services). Sous Windows, ' +
          'systemd n’existe pas : cette section reste vide par construction.'));

  const probes = s.network || [];
  const grace = s.network_grace;
  el('c-probes').innerHTML = header('Sondes réseau', `${probes.length} équipements`) +
    (probes.length
      ? `<div class="tbl-wrap"><table><thead><tr>
           <th>Équipement</th><th class="mono">Hôte</th><th class="mono">Port</th>
           <th>État</th><th class="mono">Détail</th>
         </tr></thead><tbody>` +
        probes.map((p) => `<tr>
          <td>${esc(p.label || p.name)}</td>
          <td class="mono">${esc(p.host || '—')}</td>
          <td class="mono">${esc(p.port ?? '—')}</td>
          <td>${probeBadge(p)}</td>
          <td class="mono">${esc(
              p.error ? p.error
              : (typeof p.latency_ms === 'number' ? `${Math.round(p.latency_ms)} ms` : '—'))}</td>
        </tr>`).join('') + `</tbody></table></div>` +
        (grace
          ? `<div class="unavailable" style="margin-top:.8rem"><strong>Délai de grâce en cours.</strong><br>` +
            `Les sondes ne partent qu’une fois le réseau stabilisé : une résolution mDNS ` +
            `trop précoce perturbe l’association WiFi et donnerait un faux « hors ligne ». ` +
            `Les équipements sont donc « en attente », ce qui ne signifie pas injoignable.</div>`
          : '')
      : unavailable('Aucune sonde réseau déclarée.',
          'Ce n’est pas un manque : une sonde TCP suppose de connaître une adresse à ' +
          'l’avance, l’inverse d’une découverte. Un équipement qui émet un heartbeat ' +
          'morfbeacon/1 apparaît dans Écosystème sans être déclaré nulle part. La clé ' +
          'network_services de morfsystem.json reste le dernier recours, pour un ' +
          'équipement qui ne s’annonce pas.'));
}

function renderEcosysteme(all) {
  const s = all.services || {};
  const apps = s.beacon || [];
  const offlineAfter = s.beacon_offline_after_s;

  el('c-beacon').innerHTML =
    header('Services découverts via morfBeacon', `${apps.length} annoncés`) +
    (apps.length
      ? `<div class="tbl-wrap"><table><thead><tr>
           <th>Application</th><th class="mono">Machine</th><th class="mono">Adresse</th>
           <th class="mono">Version</th>
           <th>État</th><th class="mono">Dernier heartbeat</th><th>Interface</th>
         </tr></thead><tbody>` +
        apps.map((a) => `<tr>
          <td>${esc(serviceName(a.app))}${
              !a.declared        ? ' <span class="badge badge-off">non déclaré</span>'
            : a.enabled === false ? ' <span class="badge badge-off">non supervisé</span>'
                                  : ''}</td>
          <!-- La MACHINE est le nom que l'instance ANNONCE (champ host du
               heartbeat) : c'est lui qu'on tape suivi de .local pour la
               joindre par mDNS. Un même service présent sur plusieurs
               machines occupe une ligne par machine — l'identité vient du
               champ instance du protocole, jamais du seul nom. -->
          <td class="mono">${esc(a.host || '—')}</td>
          <td class="mono">${esc(a.ip || '—')}</td>
          <td class="mono">${esc(a.version || '—')}</td>
          <!-- L'ÉTAT dit ce qu'on observe, jamais ce qu'on a déclaré. Ces deux
               faits sont indépendants : « est-ce que ça tourne ? » se constate,
               « dois-je être alerté si ça s'arrête ? » se décide. Les tester
               dans le même ordre affichait « désactivé » pour ComponentHub
               alors qu'il émettait un heartbeat trois secondes plus tôt.
               Le fait déclaratif vit maintenant en pastille près du nom.
               Hors ligne n'est rouge que si quelqu'un a promis le contraire :
               un service non supervisé qui s'arrête n'est pas une anomalie. -->
          <td>${a.online ? stateBadge(a.state || 'ok')
                : (a.enabled === false || !a.declared) ? badge('off', 'hors ligne')
                                                       : badge('err', 'hors ligne')}</td>
          <td class="mono">${esc(a.last_seen_s === undefined ? '—' : ago(a.last_seen_s))}</td>
          <td>${webUiLink(a)}</td>
        </tr>`).join('') + `</tbody></table></div>` +
        // Un paragraphe par idée, pas un pavé : le texte se consulte, il ne se
        // lit pas d'une traite. La durée n'est jamais écrite en dur — elle
        // vient de morfsystem.json, et un texte qui dirait « 60 s » mentirait
        // dès que la configuration en déciderait autrement.
        `<div class="unavailable" style="margin-top:.8rem">` +
        `<strong>Découverte automatique</strong>` +
        `<p style="margin:.6rem 0 0">morfMonitor écoute les annonces UDP ` +
        `(<code>morfbeacon/1</code>) du réseau local. Aucune adresse IP n’est ` +
        `configurée à l’avance.</p>` +
        `<p style="margin:.6rem 0 0">Les services publiant une interface Web sont ` +
        `interrogés une fois afin d’obtenir leur URL, puis les liens affichés ` +
        `pointent directement vers eux.</p>` +
        `<p style="margin:.6rem 0 0"><strong>Non déclaré</strong> signifie simplement que le ` +
        `service ne figure pas dans la liste des applications connues ` +
        `(<code>morfsystem.json</code>). Ce n’est pas une erreur.</p>` +
        `<p style="margin:.6rem 0 0">Un même service installé sur plusieurs machines ` +
        `apparaît une fois par machine.</p>` +
        `<p style="margin:.6rem 0 0">Après ${esc(offlineAfter ?? '—')} secondes sans ` +
        `heartbeat, un service est considéré hors ligne.</p></div>`
      : unavailable('Aucune annonce reçue.',
          'Aucun service morfSystem ne diffuse sur le port beacon, ou le pare-feu bloque la diffusion UDP.'));
}

// Anomalies : derivees des memes donnees, sans collecte supplementaire.
function problems(all) {
  const out = [];
  const s = all.services || {};
  const r = all.resources || {};

  // Une unite volontairement desactivee n'est pas une anomalie, et une sonde
  // « en attente » pendant le delai de grace non plus. Les signaler noierait les
  // vraies pannes sous du bruit previsible.
  (s.systemd || []).forEach((u) => {
    const st = String(u.state || '').toLowerCase();
    if (st === 'active' || st === 'disabled' || st === 'activating') return;
    out.push({ what: u.label || u.unit, state: st === 'failed' ? 'échec' : 'arrêté', kind: 'err' });
  });
  (s.network || []).forEach((p) => {
    if (String(p.state || '').toLowerCase() !== 'offline') return;
    out.push({ what: p.label || p.name, state: 'injoignable', kind: 'err' });
  });
  // Seule une application DECLAREE justifie une alerte quand elle disparait :
  // declarer, c'est dire « je m'attends a ce service ». Une application
  // simplement entendue puis arretee — un outil de bureau que l'on ferme — n'a
  // jamais ete promise a personne, et la signaler indefiniment noierait les
  // vraies pannes.
  //
  // Et la promesse porte sur le SERVICE, pas sur chacune de ses instances : la
  // liste contient desormais une ligne par machine, donc on regroupe par nom et
  // on n'alerte que si AUCUNE instance ne repond. Une machine d'essai eteinte
  // ne met pas en panne un service qui tourne tres bien ailleurs.
  const expected = new Map();
  (s.beacon || []).forEach((a) => {
    if (a.enabled === false || !a.declared) return;
    expected.set(a.app, (expected.get(a.app) || false) || !!a.online);
  });
  expected.forEach((anyOnline, app) => {
    if (!anyOnline) out.push({ what: serviceName(app), state: 'hors ligne', kind: 'err' });
  });
  [['disk', 'Stockage'], ['memory', 'Mémoire'], ['swap', 'Swap']].forEach(([k, lbl]) => {
    const p = r[k] && r[k].percent;
    if (typeof p === 'number' && p >= 90) out.push({ what: lbl, state: `${p.toFixed(0)} %`, kind: 'err' });
    else if (typeof p === 'number' && p >= 75) out.push({ what: lbl, state: `${p.toFixed(0)} %`, kind: 'warn' });
  });

  // Une sous-tension est une panne materielle silencieuse : elle corrompt la
  // carte SD et fige des services sans rien ecrire dans les journaux. Elle a sa
  // place au premier rang des anomalies, pas seulement dans une carte a lire.
  const t = r.throttling || {};
  if (t.undervoltage_now)        out.push({ what: 'Alimentation', state: 'sous-tension', kind: 'err' });
  else if (t.undervoltage_since_boot) out.push({ what: 'Alimentation', state: 'sous-tension depuis le démarrage', kind: 'warn' });
  if (t.throttled_now)           out.push({ what: 'Processeur', state: 'bridé', kind: 'err' });
  else if (t.throttled_since_boot)    out.push({ what: 'Processeur', state: 'bridé depuis le démarrage', kind: 'warn' });

  const tc = r.temperature && r.temperature.cpu_c;
  if (typeof tc === 'number' && tc >= 80)      out.push({ what: 'Température CPU', state: `${tc.toFixed(0)} °C`, kind: 'err' });
  else if (typeof tc === 'number' && tc >= 70) out.push({ what: 'Température CPU', state: `${tc.toFixed(0)} °C`, kind: 'warn' });

  return out;
}

function renderDiagnostic(all, config) {
  const pb = problems(all);
  el('c-anomalies').innerHTML =
    header('Anomalies détectées', pb.length ? `${pb.length} élément(s)` : 'aucune') +
    (pb.length
      ? pb.map((p) => row(p.what, badge(p.kind, p.state))).join('')
      : unavailable('Aucune anomalie.',
          'Tous les services supervisés répondent et aucune ressource ne dépasse 75 %.'));

  // confidence est une FRACTION (0 a 1), pas un pourcentage : l'afficher tel
  // quel donnait « 0.7 % » pour un diagnostic sur lequel le service est en fait
  // confiant a 70 %.
  const rb = all.reboot || {};
  el('c-reboot').innerHTML = header('Dernier redémarrage') +
    row('Cause', esc(rb.cause || 'inconnue')) +
    row('Confiance', typeof rb.confidence === 'number'
        ? `${Math.round(rb.confidence * 100)} %` : '—') +
    (rb.label ? `<div class="unavailable" style="margin-top:.6rem">${esc(rb.label)}</div>` : '') +
    (rb.evidence ? `<div class="unavailable" style="margin-top:.5rem">${esc(rb.evidence)}</div>` : '');

  // La configuration partagee vient de /api/config, pas de /api/all : ce dernier
  // n'expose que system, resources, network, services et reboot. Lire un
  // « all.monitor » inexistant faisait afficher « non chargee » en permanence,
  // y compris sur une machine ou elle l'etait parfaitement.
  const cfg = config || {};
  // Le pluriel porte sur le nom, pas sur le qualificatif : « services systemd »
  // et non « service systemds ».
  const counts = [
    [(cfg.systemd_services || []).length, 'service', 'services', 'systemd'],
    [(cfg.network_services || []).length, 'sonde', 'sondes', 'réseau'],
    [(cfg.beacon_apps || []).length, 'application', 'applications', 'beacon'],
  ].map(([n, one, many, qual]) => `${n} ${n > 1 ? many : one} ${qual}`).join(' · ');

  el('c-config').innerHTML = header('Configuration partagée') +
    row('Chargée', cfg.loaded ? badge('ok', 'oui') : badge('err', 'non')) +
    row('Chemin', `<span class="mono">${esc(cfg.path || '—')}</span>`) +
    (cfg.loaded ? row('Déclare', esc(counts)) : '') +
    (cfg.loaded
      ? ''
      : `<div class="unavailable" style="margin-top:.6rem">` +
        `Sans ce fichier, morfMonitor supervise la machine mais rien d’externe : ` +
        `ni service systemd, ni sonde réseau, ni application beacon déclarée. ` +
        `Emplacement attendu : /etc/morfsystem/morfsystem.json.</div>`);
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
    const [allR, statusR, configR] = await Promise.all([
      fetch('/api/all'), fetch('/status'), fetch('/api/config'),
    ]);
    const status = statusR.ok ? await statusR.json() : {};
    const config = configR.ok ? await configR.json().catch(() => ({})) : {};

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
    renderDiagnostic(all, config);

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
