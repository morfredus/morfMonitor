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

// Suivi visuel des mises à jour de service, par projet (= dépôt GitHub). Le
// tableau des services est reconstruit toutes les 5 s (et à chaque changement
// d'état) : l'avancement ne peut donc pas vivre dans le DOM, il est conservé
// ici et la cellule « Mise à jour » se redessine à partir de cet état.
// Forme : project -> { version, phase, detail, id }.
//   phase = 'confirm' | 'requesting' | 'queued' | états de l'agent morfUpdate
//           (downloading, verifying, installing, restarting, health_check) |
//           'succeeded' | 'failed' | 'rejected'.
const updateStatus = new Map();

// Dernier /api/all reçu : sert à redessiner la carte des services (et donc les
// cellules de mise à jour) sans attendre le rafraîchissement de 5 s.
let lastAll = null;

// Étapes de l'agent morfUpdate, dans l'ordre, avec un libellé lisible. Permet
// d'afficher « Vérification (2/5) » et de situer l'avancement réel.
const UPDATE_STEPS = [
  { key: 'downloading',  label: 'Téléchargement' },
  { key: 'verifying',    label: 'Vérification' },
  { key: 'installing',   label: 'Installation' },
  { key: 'restarting',   label: 'Redémarrage' },
  { key: 'health_check', label: 'Contrôle de santé' },
];

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

// Comme `ago`, mais monte jusqu'aux heures et aux jours : une machine eteinte
// peut n'avoir plus ete vue depuis longtemps, et « il y a 4320 min » ne se lit
// pas. Reserve aux machines, dont l'absence se compte en heures, pas en minutes.
function agoLong(sec) {
  if (typeof sec !== 'number') return '—';
  if (sec < 60) return `il y a ${Math.round(sec)} s`;
  if (sec < 3600) return `il y a ${Math.round(sec / 60)} min`;
  if (sec < 86400) return `il y a ${Math.round(sec / 3600)} h`;
  return `il y a ${Math.round(sec / 86400)} j`;
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

// --- Activites en cours (contrat generique `activity/1`) ---------------------
// morfMonitor decrit le PRESENT : ce qu'un service est en train de faire, en temps
// reel. La representation est la meme pour une indexation morfPhoto, une compilation
// morfDeploy, une collecte morfCollector. Les champs viennent du service ; on n'en
// deduit rien.
function actNum(n) {
  return (typeof n === 'number' && isFinite(n)) ? n.toLocaleString('fr-FR') : null;
}

function actProgress(a) {
  const cur = actNum(a.current);
  const tot = actNum(a.total);
  const pct = (typeof a.progress_percent === 'number' && isFinite(a.progress_percent))
    ? `${a.progress_percent} %` : null;
  if (cur && tot) {
    return esc(`${cur} / ${tot}`) +
      (pct ? ` <span class="info-label">(${esc(pct)})</span>` : '');
  }
  if (pct) return esc(pct);
  if (cur) return esc(cur);
  return '<span class="info-label">en cours…</span>';
}

// Duree ecoulee depuis le debut, calculee cote client (started_at est un epoch s
// serveur ; l'ecart d'horloge entre machines reste negligeable pour une duree).
function actDuration(a) {
  if (typeof a.started_at !== 'number') return '—';
  const s = Math.max(0, Math.floor(Date.now() / 1000) - a.started_at);
  if (s < 60) return `${s} s`;
  if (s < 3600) return `${Math.floor(s / 60)} min`;
  const h = Math.floor(s / 3600);
  const m = Math.floor((s % 3600) / 60);
  return `${h} h ${m} min`;
}

function activitesTable(acts) {
  return `<div class="tbl-wrap"><table><thead><tr>` +
    `<th>Service</th><th>Activité</th><th class="mono">Progression</th>` +
    `<th class="mono">Durée</th><th>Détail</th>` +
    `</tr></thead><tbody>` +
    acts.map((a) => `<tr>` +
      `<td><strong>${esc(a.service || '—')}</strong>` +
        (a.host ? ` <span class="info-label">${esc(a.host)}</span>` : '') + `</td>` +
      `<td>${esc(a.type || '—')}</td>` +
      `<td class="mono">${actProgress(a)}</td>` +
      `<td class="mono">${esc(actDuration(a))}</td>` +
      `<td>${a.detail ? esc(a.detail) : '—'}</td>` +
    `</tr>`).join('') +
    `</tbody></table></div>`;
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

// Consommation d'un service : CPU instantane et memoire du CGROUP complet de
// l'unite, telles que morfMonitor les mesure via systemd. Le bloc `resources`
// est ABSENT pour un service arrete ou desactive : non pas « 0 % », mais « non
// applicable, rien ne tourne ». On rend alors « — », qui dit la meme chose sans
// laisser croire a une mesure a zero.
function svcCpu(res) {
  if (!res || typeof res.cpu_percent !== 'number') {
    // Compteur cumule sans taux : premier releve de l'unite, le taux instantane
    // ne peut pas encore etre calcule (il faut deux mesures). On montre au moins
    // le temps CPU cumule s'il existe.
    if (res && typeof res.cpu_time_usec === 'number') {
      const d = duration(res.cpu_time_usec / 1e6);
      return d ? `<span title="temps CPU cumulé">${esc(d)}</span>` : '—';
    }
    return '—';
  }
  // Le temps CPU cumule accompagne le taux en infobulle : instantane pour la
  // charge actuelle, cumule pour le contexte, sans surcharger la colonne.
  const cum = typeof res.cpu_time_usec === 'number' ? duration(res.cpu_time_usec / 1e6) : null;
  const title = cum ? ` title="temps CPU cumulé : ${esc(cum)}"` : '';
  return `<span${title}>${res.cpu_percent.toFixed(1)} %</span>`;
}

function svcMem(res) {
  if (!res || typeof res.memory_bytes !== 'number') return '—';
  return esc(bytes(res.memory_bytes) || '—');
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

// Etat ouvert/ferme des routes API, retenu d'un rendu au suivant. La page
// reconstruit tout le tableau de l'Ecosysteme en innerHTML a chaque
// rafraichissement ; sans ce releve, chaque <details> qu'on venait d'ouvrir se
// refermait aussitot. La cle est l'identite d'INSTANCE, stable entre deux rendus
// (le meme service sur la meme machine garde donc son etat).
let openApiRows = new Set();

// Cle stable d'une ligne pour suivre l'etat de sa colonne API.
function apiRowKey(a) {
  return a.instance || (a.app + '@' + (a.ip || a.host || ''));
}

// Liste d'API qu'un service ANNONCE (champ `api` de son /status, relaye tel quel
// par /api/services). Purement descriptif : morfMonitor montre ce qu'un service
// declare offrir, il n'appelle rien. Les chemins sont ceux du service ; la base
// joignable depuis ici est dans a.api.base_url, pour qui veut composer une URL.
//
// Replie par defaut (<details>) : la cartographie ne doit pas noyer l'etat, qui
// reste l'essentiel. On deplie pour inspecter, et l'etat survit au rafraichissement.
function apiCell(a) {
  const api = a.api;
  const eps = api && Array.isArray(api.endpoints) ? api.endpoints : [];
  if (!eps.length) return '<span class="info-label">-</span>';
  const rows = eps.map((e) =>
    `<div class="mono" style="white-space:nowrap;margin:.15rem 0">` +
      `<span class="badge badge-off">${esc(e.method || 'GET')}</span> ${esc(e.path || '')}` +
      (e.summary ? `<span class="info-label"> - ${esc(e.summary)}</span>` : '') +
    `</div>`).join('');
  const n = eps.length;
  const key = apiRowKey(a);
  const open = openApiRows.has(key) ? ' open' : '';
  return `<details data-api-key="${esc(key)}"${open}><summary>${n} route${n > 1 ? 's' : ''}</summary>${rows}</details>`;
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

// État MATÉRIEL rapporté par le service (contrat morfBeacon : bloc hardware).
// On affiche le libellé TEL QUEL, sans jamais déduire la présence : « — » quand
// le service ne gère aucun matériel (bloc absent). « none » (aucun matériel
// attendu) est neutre, PAS une alerte ; seul « degraded » est en orange.
function hardwareCell(a) {
  const hw = a.hardware;
  if (!hw || !hw.state) return '—';
  const label = hw.label || hw.state;
  if (hw.state === 'present')  return badge('ok',   label);
  if (hw.state === 'degraded') return badge('warn', label);
  return badge('off', label);            // 'none' : configuration valide, neutre
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

  // Activites en cours declarees par les services du parc (temps reel).
  const acts = Array.isArray(all.activities) ? all.activities : [];
  el('c-activites').innerHTML =
    header('Activités en cours', acts.length ? `${acts.length} en cours` : 'aucune') +
    (acts.length ? activitesTable(acts)
                 : `<div class="unavailable"><strong>Aucune activité en cours.</strong><br>` +
                   `Les indexations, compilations, collectes ou synchronisations en cours ` +
                   `apparaîtront ici, déclarées par chaque service.</div>`);
}

// Schema reel de /api/resources (voir HostCollectors.cpp) :
//   cpu_percent, cpu_freq_mhz   -- A PLAT, pas dans un objet « cpu »
//   load                        -- TABLEAU [1 min, 5 min, 15 min]
//   memory, swap                -- objets { total_b, used_b, free_b, percent }
//   disks                       -- TABLEAU de { mount, total_b, used_b, free_b,
//                                  percent } : un par volume reel monte (/ ,
//                                  /home separe, /boot/firmware...)
//   disk                        -- la seule racine, conserve pour les
//                                  consommateurs d'avant `disks`
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

  // Une carte PAR VOLUME : la racine seule mentait dès que /home est une
  // partition séparée — « / » à 90 % affole alors que les données ont
  // ailleurs toute la place, et inversement un /home plein restait invisible.
  // `disks` liste les volumes réels ; `disk` (la seule racine) reste le repli
  // face à un service qui n'a pas encore été mis à jour.
  const disks = Array.isArray(r.disks) && r.disks.length ? r.disks
              : r.disk ? [r.disk] : [];
  disks.forEach((d) => {
    parts.push(`<div class="card">${header('Stockage', d.mount || '')}` +
      row('Utilisé', `${bytes(d.used_b) ?? '—'} / ${bytes(d.total_b) ?? '—'}`) +
      meter(d.percent) +
      row('Libre', bytes(d.free_b) ?? '—') +
      `</div>`);
  });

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

  const expected = { cpu_percent: 'processeur', memory: 'mémoire', load: 'charge moyenne' };
  const missing = Object.keys(expected).filter((k) => r[k] === undefined);
  if (missing.length) {
    // Depuis que Windows collecte le CPU et la mémoire, il ne manque plus
    // souvent que la charge moyenne — une notion Unix, non une limite de
    // collecte. Le message le dit alors précisément, plutôt que d'imputer à
    // « /proc » une absence qui n'en vient pas.
    const onlyLoad = missing.length === 1 && missing[0] === 'load';
    parts.push(`<div class="card span-all">${header('Métriques indisponibles')}` +
      unavailable(
        `Non collectées sur cette plateforme : ${missing.map((k) => expected[k]).join(', ')}.`,
        onlyLoad
          ? 'La charge moyenne (load average) est une notion Unix, sans équivalent ' +
            'fidèle sous Windows : le taux d’occupation du processeur répond à la même ' +
            'question. Le service est pleinement fonctionnel.'
          : 'Ces mesures proviennent de /proc et /sys : elles ne sont renseignées que ' +
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

// Badge de comparaison de version, avec info-bulle (dépôt, dernier succès,
// prerelease ignorée, raison d'un état indéterminé). L'état est CALCULÉ par le
// backend (Version::compare, sémantique) : le frontend ne fait que l'habiller.
function versionBadge(v) {
  if (!v) return '<span class="mono">—</span>';
  const st = v.state || '—';
  if (st === '—') return '<span class="mono">—</span>';   // non vérifié
  let kind = 'off';
  if (st === 'À jour' || st === 'Version locale plus récente') kind = 'ok';
  else if (st === 'Mise à jour disponible' || st === 'Vérification impossible') kind = 'warn';
  const nowS = Math.floor(Date.now() / 1000);
  const bits = [];
  if (v.repo) bits.push(`dépôt interrogé : ${v.owner || 'morfredus'}/${v.repo}`);
  if (v.last_success_s) bits.push(`dernière vérification réussie ${agoLong(nowS - v.last_success_s)}`);
  if (v.stale) bits.push('contrôle actuel impossible — dernière info connue affichée');
  else if (v.last_check_s) bits.push(`dernier contrôle ${agoLong(nowS - v.last_check_s)}`);
  if (v.error) bits.push(`détail : ${v.error}`);
  const tip = bits.join(' · ');
  return `<span class="badge badge-${kind}" title="${esc(tip)}">${esc(st)}${v.stale ? ' ⚠' : ''}</span>`;
}

// Redessine la carte des services à partir du dernier /api/all connu. Appelée à
// chaque changement d'état d'une mise à jour pour que la cellule reflète
// l'avancement sans popup et sans attendre le cycle de 5 s.
function redrawServices() {
  if (lastAll) renderServices(lastAll);
}

// Bouton « Mettre à jour » (état de repos). project = dépôt GitHub (morfDashboard),
// jamais le libellé affiché (DashBoard) ni l'unité systemd (morfdashboard) :
// c'est la clé de morfUpdate.targets.
function updateButton(v) {
  if (!v || v.state !== 'Mise à jour disponible' || !v.latest) return '';
  // L'agent refuse de se mettre à jour lui-même ; les outils n'ont pas de .deb
  // installé par ce bouton.
  if (v.updatable === false) return '';
  const project = v.project || v.repo || '';
  if (!project || project === 'morfUpdate') return '';
  return `<button class="btn-update-service" data-project="${esc(project)}" ` +
    `data-version="${esc(v.latest)}">Mettre à jour</button>`;
}

// Un échec « demandé trop tôt » : la release existe (le .deb est visible, d'où
// le bouton), mais GitHub n'a pas encore fini d'y attacher tous ses fichiers.
// morfUpdate exige manifest.json ; s'il manque, c'est presque toujours une
// demande lancée pendant la publication de la release, pas une vraie panne.
function updateTooEarly(detail) {
  const d = String(detail || '').toLowerCase();
  return d.includes('manifest.json')
      || d.includes('manifest asset is absent')
      || d.includes('releases/tags');
}

// Message d'échec explicite : dit s'il s'agit d'une demande trop précoce et
// quoi faire, plutôt que de recracher la raison technique brute.
function updateFailureText(detail) {
  if (updateTooEarly(detail)) {
    return 'Demande trop tôt : la version vient d’être publiée et GitHub n’a ' +
      'pas encore attaché tous ses fichiers (manifest.json manquant). ' +
      'Attendre une minute, cliquer « Vérifier les versions », puis relancer.';
  }
  return `Mise à jour non terminée : ${detail || 'raison inconnue'}.`;
}

// Rendu de la cellule « Mise à jour » : badge de comparaison de version, puis
// soit le bouton, soit la confirmation en ligne, la progression ou le résultat.
function updateCell(v) {
  const project = (v && (v.project || v.repo)) || '';
  const st = project ? updateStatus.get(project) : null;
  const badge = versionBadge(v);
  if (!st) return `${badge} ${updateButton(v)}`;

  const version = esc(st.version || '');
  if (st.phase === 'confirm') {
    // La question se pose dans la cellule, pas dans un popup.
    return `${badge} <span class="upd">` +
      `<span class="upd-ask">Passer à ${version} ?</span>` +
      `<button class="btn-update-confirm" data-project="${esc(project)}">Confirmer</button>` +
      `<button class="btn-update-cancel" data-project="${esc(project)}">Annuler</button>` +
      `</span>`;
  }
  if (st.phase === 'succeeded') {
    return `${badge} <span class="upd-done">✓ ${version} installé et vérifié</span>`;
  }
  if (st.phase === 'failed' || st.phase === 'rejected') {
    return `${badge} <span class="upd upd-fail">` +
      `<span class="upd-fail-msg">✗ ${esc(updateFailureText(st.detail))}</span>` +
      `<button class="btn-update-retry" data-project="${esc(project)}" data-version="${version}">Réessayer</button>` +
      `<button class="btn-update-dismiss" data-project="${esc(project)}">Masquer</button>` +
      `</span>`;
  }
  // En cours : barre indéterminée qui défile sous le service + étape courante.
  const idx = UPDATE_STEPS.findIndex((s) => s.key === st.phase);
  const label = st.phase === 'requesting' ? 'Demande envoyée'
    : st.phase === 'queued' ? 'En file d’attente'
    : (idx >= 0 ? UPDATE_STEPS[idx].label : 'En cours');
  const counter = idx >= 0 ? ` (${idx + 1}/${UPDATE_STEPS.length})` : '';
  return `${badge} <span class="upd-run">` +
    `<span class="upd-bar"><i></i></span>` +
    `<span class="upd-step">${esc(label)}${counter}…</span></span>`;
}

// Lance (ou relance) une mise à jour : POST vers morfMonitor, qui proxy vers
// l'agent morfUpdate local. Aucun popup — tout retour passe par updateStatus.
async function launchUpdate(project, version) {
  if (!project || !version) return;
  updateStatus.set(project, { version, phase: 'requesting' });
  redrawServices();
  try {
    const response = await fetch('/api/updates', {
      method: 'POST', headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({ project, version }),
    });
    const result = await response.json().catch(() => ({}));
    if (!response.ok && response.status !== 202) {
      let detail = result.error || 'demande refusée';
      if (response.status === 400) {
        detail = `« ${project} » n’est pas une cible déclarée dans ` +
          'morfupdate.json. Aligner la config (config push --force depuis le ' +
          'clone morfUpdate), puis redémarrer morfupdate.';
      } else if (response.status === 503) {
        detail = 'agent de mise à jour injoignable. Vérifier morfupdate : ' +
          'curl http://127.0.0.1:8794/healthz';
      }
      updateStatus.set(project, { version, phase: 'failed', detail });
      redrawServices();
      return;
    }
    if (result.id) {
      updateStatus.set(project, { id: result.id, version, phase: 'queued' });
      redrawServices();
      followUpdate(project, result.id, version);
    }
  } catch (error) {
    updateStatus.set(project, { version, phase: 'failed',
      detail: `demande impossible : ${error.message}` });
    redrawServices();
  }
}

// Suit une opération jusqu'à son terme en interrogeant /api/updates/<id>.
// Ne redessine que lorsque l'état change réellement, pour ne pas relancer
// l'animation de la barre à chaque sondage.
async function followUpdate(project, id, version, attempt = 0) {
  const retry = () => {
    // dpkg coupe momentanément morfMonitor pendant l'installation : une réponse
    // 5xx passagère (ou un « Failed to fetch ») n'est pas un échec d'install.
    setConn('warn', 'redémarrage…');
    setTimeout(() => followUpdate(project, id, version, attempt + 1), 2000);
  };
  try {
    const response = await fetch(`/api/updates/${encodeURIComponent(id)}`);
    if (!response.ok && response.status >= 500 && attempt < 90) { retry(); return; }
    const result = await response.json();
    if (!response.ok) throw new Error(result.error || 'suivi indisponible');
    const state = result.state || 'queued';

    const current = updateStatus.get(project);
    if (!current || current.phase !== state || current.detail !== result.detail) {
      updateStatus.set(project, { id, version, phase: state, detail: result.detail });
      redrawServices();
    }

    if (state === 'succeeded') {
      refresh();                                   // relit la version exécutée
      // Le succès reste lisible un moment, puis s'efface de lui-même.
      setTimeout(() => { updateStatus.delete(project); redrawServices(); }, 12000);
      return;
    }
    if (state === 'failed' || state === 'rejected') {
      refresh();
      return;                                      // reste affiché jusqu'à « Masquer »
    }
    setTimeout(() => followUpdate(project, id, version, 0), 1500);
  } catch (error) {
    if (attempt < 90) { retry(); return; }
    updateStatus.set(project, { id, version, phase: 'failed',
      detail: `suivi impossible : ${error.message}` });
    redrawServices();
  }
}

function renderServices(all) {
  const s = all.services || {};

  // Versions par service : la partie release vient du backend (via morfUpdate),
  // déjà jointe à la version exécutée annoncée par le beacon. Indexée par le nom
  // affiché du service (= label systemd).
  const verByLabel = {};
  (s.versions || []).forEach((v) => { verByLabel[v.service] = v; });
  const lastSuccess = (s.versions || [])
    .map((v) => v.last_success_s || 0)
    .reduce((a, b) => Math.max(a, b), 0);
  const nowS = Math.floor(Date.now() / 1000);
  const checkMeta = lastSuccess
    ? `versions vérifiées ${agoLong(nowS - lastSuccess)}`
    : 'versions jamais vérifiées';

  // Nom de CETTE machine : la version exécutée d'un service local devrait venir
  // d'elle. Si elle vient d'un autre hôte (même service ailleurs dans le parc), on
  // le signale plutôt que d'afficher un numéro sans provenance.
  const localHost = (all.system && all.system.hostname) || '';
  const runCell = (v) => {
    if (!v || !v.running) return '—';
    const foreign = v.running_host && localHost &&
      v.running_host.toLowerCase() !== localHost.toLowerCase();
    return foreign
      ? `${esc(v.running)} <span class="badge badge-off" title="Version annoncée par ${esc(v.running_host)}, pas par cette machine">${esc(v.running_host)}</span>`
      : esc(v.running);
  };

  const units = s.systemd || [];
  const versionsBar =
    `<div class="unavailable" style="display:flex;align-items:center;justify-content:space-between;margin:.2rem 0 .6rem">` +
    `<span>Comparaison avec la dernière release publiée sur GitHub — ${esc(checkMeta)}.</span>` +
    `<button class="btn-check-versions">Vérifier les versions</button></div>`;

  el('c-systemd').innerHTML = header('Services systemd', `${units.length} supervisés`) +
    (units.length
      ? versionsBar +
        `<div class="tbl-wrap"><table><thead><tr>
           <th>Service</th><th class="mono">Unité</th><th>État</th>
           <th class="mono">Version exécutée</th><th class="mono">Dernière release</th><th>Mise à jour</th>
           <th class="mono">CPU</th><th class="mono">Mémoire</th><th class="mono">Détail</th>
         </tr></thead><tbody>` +
        units.map((u) => {
          const v = verByLabel[u.label || u.unit];
          return `<tr>
          <td>${esc(u.label || u.unit)}</td>
          <td class="mono">${esc(u.unit || '—')}</td>
          <td>${systemdBadge(u)}</td>
          <td class="mono">${runCell(v)}</td>
          <td class="mono">${esc((v && v.latest) || '—')}</td>
          <td>${updateCell(v)}</td>
          <td class="mono">${svcCpu(u.resources)}</td>
          <td class="mono">${svcMem(u.resources)}</td>
          <td class="mono">${esc(u.sub_state || u.state || '—')}</td>
        </tr>`;
        }).join('') + `</tbody></table></div>`
      : unavailable('Aucun service systemd supervisé.',
          'La liste vient de morfsystem.json (clé systemd_services). Sous Windows, ' +
          'systemd n’existe pas : cette section reste vide par construction.'));

  const eco = (s.versions || []).filter((v) => v.group === 'ecosystem');
  el('c-ecosystem-libs').innerHTML = header('Bibliothèques et outils', `${eco.length}`) +
    (eco.length
      ? `<div class="unavailable" style="margin:.2rem 0 .6rem">` +
        `Releases GitHub, hors morfSystem. Beacon et Deploy : copie vendorée compilée dans ` +
        `ce morfMonitor. Packages et Tools : fichier VERSION d’un clone voisin s’il existe. ` +
        `Pas de bouton : ces projets ne s’installent pas via l’agent local.</div>` +
        `<div class="tbl-wrap"><table><thead><tr>
           <th>Projet</th><th>Rôle</th>
           <th class="mono">Version locale</th><th class="mono">Dernière release</th>
           <th>Mise à jour</th>
         </tr></thead><tbody>` +
        eco.map((v) => `<tr>
          <td>${esc(v.service)}</td>
          <td class="mono">${esc(v.kind || '—')}</td>
          <td class="mono">${runCell(v)}</td>
          <td class="mono">${esc(v.latest || '—')}</td>
          <td>${versionBadge(v)}</td>
        </tr>`).join('') + `</tbody></table></div>`
      : unavailable('Aucun projet d’écosystème déclaré.',
          'Ajouter ecosystem_projects dans morfsystem.json (morfBeacon, morfDeploy, morfPackages, morfTools), puis déployer la config partagée.'));

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

// Etat lisible d'une machine du parc (registre persistant appris par beacon).
function machineStateBadge(m) {
  if (m.state === 'active')   return badge('ok',  'active');
  if (m.state === 'archived') return badge('off', 'archivée');
  return badge('warn', 'éteinte');   // offline : connue, mais silencieuse
}

// Machines du parc : la memoire, par MACHINE, de ce que morfMonitor a decouvert.
// Un poste entierement eteint tient ici en UNE ligne (« pi4dev — éteinte — vue il
// y a 3 h »), au lieu de faire clignoter en rouge chacun de ses services dans le
// tableau ci-dessous. L'absence d'une machine connue est elle-meme une
// information ; on la garde, sans la confondre avec une panne de service.
function renderMachines(all) {
  const s = all.services || {};
  const machines = (s.machines || []).slice()
    // Actives d'abord, puis par nom : la vue reste stable d'un rafraichissement
    // a l'autre, et ce qui vit remonte au-dessus de ce qui dort.
    .sort((a, b) => (a.online === b.online)
      ? String(a.host).localeCompare(String(b.host))
      : (a.online ? -1 : 1));

  const active = machines.filter((m) => m.state === 'active').length;

  el('c-machines').innerHTML =
    header('Machines du parc', `${machines.length} connue${machines.length > 1 ? 's' : ''}` +
      (machines.length ? ` · ${active} active${active > 1 ? 's' : ''}` : '')) +
    (machines.length
      ? `<div class="tbl-wrap"><table><thead><tr>
           <th class="mono">Machine</th><th>État</th>
           <th class="mono">Vue</th><th></th>
         </tr></thead><tbody>` +
        machines.map((m) => `<tr>
          <td class="mono">${esc(m.host)}</td>
          <td>${machineStateBadge(m)}</td>
          <td class="mono">${m.online ? '—' : esc(agoLong(m.last_seen_s))}</td>
          <td>${m.state === 'active' ? ''
            : `<button class="btn-forget" data-host="${esc(m.host)}" title="Retirer cette machine du parc">Oublier</button>`}</td>
        </tr>`).join('') + `</tbody></table></div>` +
        `<div class="unavailable" style="margin-top:.8rem">` +
        `<p style="margin:0">Ces machines sont apprises seules à partir des annonces ` +
        `morfBeacon : aucune n’est déclarée à la main. Une machine éteinte reste ` +
        `mémorisée, puis passe en <em>archivée</em> après une longue absence, sans ` +
        `être supprimée. <strong>Oublier</strong> la retire définitivement — à ` +
        `n’utiliser que pour une machine réellement partie du parc.</p></div>`
      : unavailable('Aucune machine connue pour l’instant.',
          'Dès qu’un poste (rôle host) diffuse un heartbeat morfBeacon, il est mémorisé ici.'));
}

// Une machine est-elle eteinte, du point de vue du registre ? Sert a masquer du
// tableau des services les entrees d'un poste hors ligne : la machine parle pour
// elles dans la carte « Machines du parc », inutile de repeindre huit lignes.
function offlineHosts(all) {
  const s = all.services || {};
  const set = new Set();
  (s.machines || []).forEach((m) => { if (!m.online) set.add(m.host); });
  return set;
}

function renderEcosysteme(all) {
  // Releve l'etat des routes depliees AVANT de reconstruire le tableau, pour le
  // restaurer a l'identique. Sans cela, le rafraichissement periodique refermait
  // ce que l'utilisateur venait d'ouvrir.
  openApiRows = new Set();
  document.querySelectorAll('#c-beacon details[data-api-key]').forEach((d) => {
    if (d.open) openApiRows.add(d.getAttribute('data-api-key'));
  });

  const s = all.services || {};
  // Masque les services portes par une machine ETEINTE : la carte « Machines du
  // parc » les represente deja par une seule ligne. On ne cache que les services
  // (role host) d'un poste hors ligne ; un equipement (role device) garde sa
  // ligne, car il n'est pas rattache a une machine generaliste. Un poste ALLUME
  // montre au contraire tous ses services, comme avant.
  const off = offlineHosts(all);
  const apps = (s.beacon || []).filter(
    (a) => !(a.role !== 'device' && a.host && off.has(a.host)));
  const hidden = (s.beacon || []).length - apps.length;
  const offlineAfter = s.beacon_offline_after_s;

  el('c-beacon').innerHTML =
    header('Services découverts via morfBeacon',
      `${apps.length} annoncé${apps.length > 1 ? 's' : ''}` +
      (hidden ? ` · ${hidden} masqué${hidden > 1 ? 's' : ''} (poste éteint)` : '')) +
    (apps.length
      ? `<div class="tbl-wrap"><table><thead><tr>
           <th>Application</th><th class="mono">Machine</th><th class="mono">Adresse</th>
           <th class="mono">Port</th><th class="mono">Version</th>
           <th>État</th><th>Matériel</th><th class="mono">Dernier heartbeat</th><th>Interface</th><th>API</th>
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
          <!-- Le PORT est celui du /status annoncé dans le heartbeat
               (champ status_port) : c'est par lui que le service se joint,
               et il diffère d'un service à l'autre (8790 morfMonitor,
               8789 morfNotify...). Absent tant qu'aucune instance n'a été
               entendue (application déclarée mais hors ligne). -->
          <td class="mono">${a.status_port ? esc(a.status_port) : '—'}</td>
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
          <!-- Matériel : ce que le SERVICE déclare (present/none/degraded), affiché
               tel quel. morfMonitor n'infère jamais la présence du matériel. -->
          <td>${a.online ? hardwareCell(a) : '—'}</td>
          <td class="mono">${esc(a.last_seen_s === undefined ? '—' : ago(a.last_seen_s))}</td>
          <td>${webUiLink(a)}</td>
          <td>${apiCell(a)}</td>
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
  // La promesse porte sur le SERVICE, pas sur chacune de ses instances, et une
  // MACHINE eteinte n'est pas une panne de service. On distingue donc trois cas,
  // par application declaree :
  //   - au moins une instance en ligne            -> tout va bien ;
  //   - hors ligne sur une machine ALLUMEE         -> panne reelle, attribuable
  //     (le service devrait tourner la, et il n'y est pas) : anomalie rouge ;
  //   - connue seulement sur des machines ETEINTES -> ce n'est pas le service qui
  //     est en cause mais la machine : la carte « Machines du parc » le montre,
  //     pas d'anomalie de service ici ;
  //   - entendue NULLE PART                         -> service attendu introuvable
  //     dans tout le parc : anomalie rouge.
  // host_online (fourni par morfMonitor) porte exactement cette distinction.
  const agg = new Map();   // app -> { online, downOnLiveHost, sawInstance }
  (s.beacon || []).forEach((a) => {
    if (a.enabled === false || !a.declared) return;
    const g = agg.get(a.app) || { online: false, downOnLiveHost: false, sawInstance: false };
    // Une entree « declaree mais entendue nulle part » n'a pas d'hote : on ne la
    // compte pas comme une instance vue.
    const isInstance = !!a.host;
    if (isInstance) g.sawInstance = true;
    if (a.online) g.online = true;
    else if (isInstance && a.host_online) g.downOnLiveHost = true;
    agg.set(a.app, g);
  });
  agg.forEach((g, app) => {
    if (g.online) return;                       // tourne quelque part : rien a dire
    if (g.downOnLiveHost)
      out.push({ what: serviceName(app), state: 'hors ligne', kind: 'err' });
    else if (!g.sawInstance)
      out.push({ what: serviceName(app), state: 'introuvable', kind: 'err' });
    // Sinon : uniquement sur des machines eteintes -> pas d'anomalie de service.
  });
  // Chaque volume est surveillé pour lui-même : un /home qui se remplit est
  // une anomalie que la seule racine ne montrait jamais. Le point de montage
  // figure dans le libellé, sinon deux volumes pleins donnent deux lignes
  // « Stockage » impossibles à distinguer.
  const vols = Array.isArray(r.disks) && r.disks.length ? r.disks
             : r.disk ? [r.disk] : [];
  const gauges = vols.map((d) => [d.percent, `Stockage ${d.mount || ''}`.trim()]);
  gauges.push([r.memory && r.memory.percent, 'Mémoire'],
              [r.swap && r.swap.percent, 'Swap']);
  gauges.forEach(([p, lbl]) => {
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
    [(cfg.ecosystem_projects || []).length, 'projet', 'projets', 'd’écosystème'],
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
  ['c-machine', 'c-interfaces', 'c-systemd', 'c-ecosystem-libs', 'c-beacon', 'c-anomalies'].forEach((id) => {
    const node = el(id);
    if (node) node.innerHTML = block;
  });
  ['c-sante', 'c-apercu', 'c-probes', 'c-reboot', 'c-config', 'c-machines'].forEach((id) => {
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
    lastAll = all;   // permet de redessiner les services hors du cycle de 5 s

    el('hdr-version').textContent = status.version ? `v${status.version}` : '';
    el('hdr-host').textContent = (all.system && all.system.hostname) || status.host || '';

    renderEtat(all, status);
    renderRessources(all);
    renderReseau(all);
    renderServices(all);
    renderMachines(all);
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

// « Oublier une machine » : geste explicite et irreversible, donc confirme. La
// carte est reconstruite a chaque rafraichissement, d'ou une delegation d'evenement
// sur le conteneur plutot qu'un handler par bouton.
// Bouton « Vérifier les versions » : force une vérification FRAÎCHE (même cache
// valide) côté backend, puis rafraîchit l'affichage. Non bloquant : les résultats
// arrivent en arrière-plan, on relit après un court délai.
el('page-services').addEventListener('click', async (ev) => {
  const btn = ev.target.closest('button.btn-check-versions');
  if (!btn) return;
  btn.disabled = true;
  btn.textContent = 'Vérification…';
  try {
    await fetch('/api/versions/check', { method: 'POST' });
  } catch (_) { /* le backend garde la dernière info connue */ }
  // Laisse le temps aux requêtes GitHub d'aboutir, puis relit (plusieurs fois).
  setTimeout(refresh, 2500);
  setTimeout(refresh, 6000);
});

// Mises à jour de service : un seul écouteur, attaché une fois. Le tableau étant
// reconstruit en continu, des handlers par bouton seraient reposés à chaque
// redraw ; la délégation survit aux reconstructions et couvre les quatre gestes
// (lancer, confirmer, annuler/masquer, réessayer) sans le moindre popup.
el('c-systemd').addEventListener('click', (ev) => {
  const btn = ev.target.closest('button');
  if (!btn) return;
  const project = btn.dataset.project;
  if (!project) return;

  if (btn.classList.contains('btn-update-service')) {
    // Pas de popup : la confirmation s'affiche dans la cellule elle-même.
    updateStatus.set(project, { version: btn.dataset.version, phase: 'confirm' });
    redrawServices();
  } else if (btn.classList.contains('btn-update-cancel')
          || btn.classList.contains('btn-update-dismiss')) {
    updateStatus.delete(project);
    redrawServices();
  } else if (btn.classList.contains('btn-update-confirm')
          || btn.classList.contains('btn-update-retry')) {
    const known = updateStatus.get(project) || {};
    launchUpdate(project, btn.dataset.version || known.version);
  }
});

el('c-machines').addEventListener('click', async (ev) => {
  const btn = ev.target.closest('button.btn-forget');
  if (!btn) return;
  const host = btn.getAttribute('data-host');
  if (!host || !window.confirm(
      `Oublier définitivement « ${host} » ?\n\n` +
      `La machine sera retirée du registre. Si elle se remet à émettre, ` +
      `elle sera réapprise automatiquement.`))
    return;
  btn.disabled = true;
  try {
    const r = await fetch('/api/machines/forget', {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({ host }),
    });
    if (!r.ok) {
      let msg = `HTTP ${r.status}`;
      try { msg = (await r.json()).error || msg; } catch (_) { /* corps non JSON */ }
      window.alert(`Impossible d’oublier « ${host} » : ${msg}`);
      btn.disabled = false;
      return;
    }
    refresh();   // la machine disparait de la carte au prochain rendu
  } catch (e) {
    window.alert(`Impossible d’oublier « ${host} » : ${e.message}`);
    btn.disabled = false;
  }
});

const initial = location.hash.replace('#', '');
if (initial) {
  const btn = document.querySelector(`#nav button[data-page="${CSS.escape(initial)}"]`);
  if (btn) btn.click();
}

refresh();
setInterval(refresh, REFRESH_MS);
