# Accès distant à morfMonitor (WireGuard)

morfMonitor sert son interface et son API en clair sur le réseau local, sans
authentification : c'est le modèle de confiance du parc, « confiance = réseau
local ». Ce choix reste valable tant que l'accès se fait depuis le LAN. Pour
atteindre morfMonitor depuis l'extérieur (en déplacement, chez un tiers), il ne
faut donc PAS ajouter une authentification propre au service, mais placer un
**composant dédié** qui arbitre la confiance à l'entrée, sans que les services
existants aient à changer.

Ce composant est un **VPN WireGuard hébergé sur le Raspberry Pi**. Une fois le
tunnel monté, le poste distant se comporte comme s'il était sur le LAN : il
ouvre `http://<ip-vpn-du-pi>:8790` exactement comme à la maison. morfMonitor ne
sait pas, et n'a pas besoin de savoir, qu'il est atteint de l'extérieur.

## Principe

- **Un seul point d'entrée.** Le tunnel WireGuard est la seule ouverture vers
  Internet. Il est chiffré et muet sans la bonne clé : un scan de port ne révèle
  rien.
- **Aucune authentification par service.** La confiance est tranchée par la clé
  WireGuard, une fois, à la frontière. morfMonitor, morfAnalytics et les autres
  gardent leur interface LAN inchangée.
- **Aucun intervenant externe.** C'est la box de la maison qui écoute et le Pi
  qui répond. Rien ne transite par un tiers.

## Ce qu'il faut réunir

| Élément | Rôle |
|---|---|
| WireGuard installé sur le Pi | Le serveur du tunnel (composant d'accès distant). |
| Un port UDP redirigé sur la box | Laisse entrer le tunnel jusqu'au Pi (par défaut `51820/UDP`). |
| Une IP LAN fixe pour le Pi | La redirection de port doit viser une adresse stable (réservation DHCP sur la box). |
| Un nom DynDNS | Suit l'IP publique de la box, qui peut changer. La résolution du nom ne fait passer aucun trafic par le fournisseur DynDNS. |
| Le client WireGuard sur chaque appareil | Poste fixe, portable, téléphone Android : import d'un fichier de configuration ou d'un QR code. |

## Mise en place (résumé)

1. **Sur le Pi** : installer WireGuard, générer une paire de clés pour le serveur
   et une par appareil client, écrire `/etc/wireguard/wg0.conf` (un réseau privé
   dédié au tunnel, par exemple `10.6.0.0/24`, le Pi en `10.6.0.1`), puis activer
   le service : `sudo systemctl enable --now wg-quick@wg0`.
2. **Sur la box** : réserver l'IP du Pi, puis rediriger `51820/UDP` vers cette IP.
   C'est la seule ouverture vers Internet.
3. **DynDNS** : créer un sous-domaine et poser un petit updater sur le Pi (tâche
   périodique) pour que le nom suive l'IP publique. Renseigner ce nom comme
   `Endpoint` dans les configurations clientes.
4. **Sur chaque appareil** : importer la configuration cliente (fichier `.conf`
   sur un poste, QR code sur Android via l'application WireGuard officielle).

> Les fichiers de configuration et les QR contiennent des **clés privées**. Ne
> pas les publier ni les partager. Ce guide décrit la méthode ; il ne contient
> aucune clé, aucun jeton ni aucune adresse réelle.

## Usage au quotidien

1. Activer le tunnel WireGuard (poste ou téléphone).
2. Ouvrir l'interface de morfMonitor sur l'adresse VPN du Pi, par exemple
   `http://10.6.0.1:8790`. Les autres services du parc se joignent de la même
   façon, chacun sur son port (voir la colonne « Port » de la page Écosystème).
3. Couper le tunnel une fois terminé.

Le nom mDNS (`.local`) ne traverse pas le tunnel : passer par l'adresse IP du Pi
sur le réseau du VPN, pas par son nom d'hôte local.

## En cas de panne d'accès depuis l'extérieur

Neuf fois sur dix, la cause n'est pas WireGuard mais l'un des maillons en amont :

- Le Pi est éteint, ou hors ligne.
- La redirection de port sur la box a sauté (remise à zéro, changement de box).
- L'IP publique a changé et le nom DynDNS ne s'est pas encore mis à jour.

Vérifier d'abord la présence du Pi et l'état de la redirection avant de suspecter
le tunnel lui-même.
