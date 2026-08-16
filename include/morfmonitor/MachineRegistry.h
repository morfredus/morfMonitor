/*
 * morfMonitor
 * Copyright (C) 2026 morfredus
 * SPDX-License-Identifier: GPL-3.0-only
 */

#pragma once
#include <QString>
#include <QHash>
#include <QJsonArray>

namespace morfmonitor {

// -----------------------------------------------------------------------------
// MachineRegistry : memoire PERSISTANTE des machines (role « host ») du parc,
// apprise seule a partir des heartbeats morfBeacon.
//
// Pourquoi ce registre existe
// ---------------------------
// La presence beacon est volatile : une machine eteinte cesse simplement
// d'emettre, et morfMonitor finit par oublier ses instances. On perd alors une
// information pourtant utile : cette machine EXISTE, elle faisait partie du parc.
// Sans memoire, impossible de distinguer « je n'ai jamais connu cette machine »
// de « cette machine etait la, et elle n'est plus ».
//
// Le registre comble ce trou SANS declaration manuelle : c'est l'un des interets
// de morfBeacon que le parc se decrive lui-meme. Au premier heartbeat valide d'un
// poste, on memorise la machine ; elle survit ensuite a son extinction et a un
// redemarrage de morfMonitor.
//
// Etats (derives de la date de derniere annonce, jamais stockes comme tels)
// --------------------------------------------------------------------------
//   active   : heartbeat recent (moins de `offlineAfterS`).
//   offline  : connue, plus aucun heartbeat, mais absence encore recente.
//   archived : absente depuis longtemps (plus de `archiveAfterS`). Rangee hors de
//              la vue principale, JAMAIS supprimee : une machine rarement allumee
//              ne doit pas disparaitre en silence. Elle redevient `active` d'elle
//              meme si elle se remet a emettre.
//
// La suppression definitive est un geste EXPLICITE de l'utilisateur (« Oublier
// cette machine »), le seul moyen de retirer une machine reellement partie.
//
// Ce registre ne connait QUE les machines (role « host »). Les equipements
// (role « device ») vivent leur propre presence et ne sont pas des machines.
// -----------------------------------------------------------------------------
class MachineRegistry {
public:
    // Resout le fichier d'etat sous `stateDir` et charge le contenu existant.
    // Un stateDir vide ou non inscriptible desactive simplement la persistance :
    // le registre fonctionne en memoire, il ne fait jamais echouer le service.
    void load(const QString& stateDir);

    // A appeler pour chaque heartbeat d'un poste (role « host »). Memorise la
    // machine au premier contact et met a jour sa derniere annonce. Retourne true
    // si une machine INCONNUE vient d'etre apprise (l'appelant peut le journaliser).
    bool observe(const QString& host, qint64 nowSec);

    // Supprime definitivement une machine. Persiste aussitot. Retourne false si
    // la machine n'etait pas connue.
    bool forget(const QString& host);

    // Machines connues avec leur etat derive au moment `nowSec`. `offlineAfterS`
    // est le seuil hors-ligne (aligne sur le beacon) ; `archiveAfterS` la duree
    // d'absence avant archivage automatique.
    QJsonArray machinesJson(qint64 nowSec, int offlineAfterS, qint64 archiveAfterS) const;

    // Ecrit le registre si besoin (derniere annonce comprise). Appele a l'arret
    // et de loin en loin ; sans effet si rien n'a change ou si la persistance est
    // desactivee.
    void flush();

private:
    struct Machine {
        qint64 firstSeen = 0;   // premier heartbeat jamais entendu
        qint64 lastSeen  = 0;   // dernier heartbeat entendu
    };

    void save();

    QString                 m_path;              // vide => persistance desactivee
    QHash<QString, Machine> m_machines;          // clef = nom d'hote
    bool                    m_dirty      = false;
    qint64                  m_lastSaveSec = 0;    // pour espacer les ecritures disque

    // La derniere annonce bouge toutes les 15 s : ecrire le fichier a chaque fois
    // userait la carte SD d'un Raspberry Pi pour rien. On persiste une nouvelle
    // machine et un oubli AUSSITOT (evenements rares et importants), mais on ne
    // flushe la simple mise a jour de last_seen qu'au plus une fois par cette
    // periode -- assez frais pour un « vu il y a 3 h » juste apres un redemarrage.
    static constexpr qint64 kMinSaveIntervalS = 300;   // 5 min
};

} // namespace morfmonitor
