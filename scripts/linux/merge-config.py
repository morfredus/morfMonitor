#!/usr/bin/env python3
"""Fusionne les NOUVELLES cles de la configuration d'exemple dans la
configuration deployee, sans jamais toucher aux valeurs deja en place.

Pourquoi ce script existe
-------------------------
La configuration deployee ne doit pas etre ecrasee lors d'une mise a jour : elle
porte les reglages locaux (adresse de l'appareil, altitude, port). Mais ne jamais
la toucher est tout aussi faux : une version qui introduit un parametre le voit
rester absent indefiniment, et la fonction correspondante ne s'active jamais sans
que rien ne le signale. C'est exactement ce qui est arrive aux parametres
`source_url` et `altitude_m` : ajoutes a l'exemple, jamais livres aux
installations existantes, donc aucune collecte.

Regle appliquee : on AJOUTE ce qui manque, on ne MODIFIE jamais ce qui existe.

Usage :
    merge-config.py <exemple.json> <deployee.json> [--dry-run]

Sortie : la liste des cles ajoutees, une par ligne. Code de retour 0 meme si
rien n'a ete ajoute (ce n'est pas une erreur).
"""

import json
import sys


def merge(example, live, path=""):
    """Ajoute recursivement dans `live` les cles absentes, presentes dans
    `example`. Renvoie la liste des chemins ajoutes."""
    added = []
    for key, value in example.items():
        # Les cles de commentaire documentent le MODELE ; les deverser dans la
        # configuration deployee l'encombrerait sans rien apporter, le script
        # listant deja ce qu'il a ajoute.
        if key.startswith("_comment"):
            continue
        full = f"{path}.{key}" if path else key

        if key not in live:
            live[key] = value
            added.append(full)
            continue

        # La cle existe des deux cotes : on ne descend que si les deux sont des
        # objets. Une valeur deja renseignee n'est jamais remplacee.
        if isinstance(value, dict) and isinstance(live[key], dict):
            added += merge(value, live[key], full)
        elif key == "modules" and isinstance(value, list) and isinstance(live[key], list):
            added += merge_modules(value, live[key], full)
    return added


def merge_modules(example_mods, live_mods, path):
    """Les modules sont une liste d'objets, pas un dictionnaire : on apparie par
    `id`, a defaut par `type`. On ne cree jamais de module absent localement —
    ce serait activer une fonction que l'utilisateur n'a pas demandee — mais on
    complete ceux qui existent."""
    added = []
    for ex in example_mods:
        if not isinstance(ex, dict):
            continue
        key = ex.get("id") or ex.get("type")
        match = None
        for lm in live_mods:
            if not isinstance(lm, dict):
                continue
            if (lm.get("id") or lm.get("type")) == key or lm.get("type") == ex.get("type"):
                match = lm
                break
        if match is not None:
            added += merge(ex, match, f"{path}[{key}]")
    return added


def main():
    args = [a for a in sys.argv[1:] if not a.startswith("--")]
    dry_run = "--dry-run" in sys.argv
    if len(args) != 2:
        print(__doc__.strip(), file=sys.stderr)
        return 2

    example_path, live_path = args
    try:
        with open(example_path, encoding="utf-8") as f:
            example = json.load(f)
        with open(live_path, encoding="utf-8") as f:
            live = json.load(f)
    except (OSError, ValueError) as exc:
        # Une config deployee illisible ne doit pas faire echouer la mise a jour
        # du binaire : on le signale et on laisse le fichier intact.
        print(f"fusion impossible : {exc}", file=sys.stderr)
        return 1

    added = merge(example, live)

    if added and not dry_run:
        with open(live_path, "w", encoding="utf-8") as f:
            json.dump(live, f, indent=2, ensure_ascii=False)
            f.write("\n")

    for key in added:
        print(key)
    return 0


if __name__ == "__main__":
    sys.exit(main())
