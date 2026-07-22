#!/usr/bin/env python3
"""Verifie qu'une configuration deployee produira un service FONCTIONNEL.

Pourquoi ce script existe
-------------------------
`merge-config.py` AJOUTE les cles manquantes sans jamais modifier ce qui existe.
C'est la bonne regle : la configuration deployee porte des reglages locaux que
l'utilisateur ne pourrait pas retrouver.

Mais cette regle laisse un angle mort : une valeur DEJA PRESENTE qui est devenue
invalide n'est jamais signalee. C'est exactement ce qui est arrive au module
`example` — herite du gabarit, jamais reconnu par la fabrique de morfMonitor.
La cle `modules` existait, donc la fusion n'y touchait pas ; le service demarrait,
ecoutait, annoncait sa presence sur le LAN... et repondait 503 sur TOUTES les
routes /api/, faute du moindre module de supervision.

Ce script ne modifie rien. Il constate, et il explique.

Les types valides sont demandes AU BINAIRE (`--list-types`) plutot que codes en
dur : la verification reste juste quand la fabrique evolue.

Usage :
    check-config.py <config.json> [--binary <chemin>] [--example <exemple.json>]

Code de retour :
    0  configuration exploitable (avertissements possibles)
    1  configuration inexploitable : le service demarrera sans rien superviser
    2  erreur d'usage (fichier illisible, JSON invalide)
"""

import argparse
import json
import os
import subprocess
import sys

ERR, WARN, OK, INFO = "[ERREUR]", "[ATTENTION]", "[OK]", "[INFO]"

# Ce script est appele des deux plateformes (config-tool.sh et config-tool.ps1).
# Un conseil qui cite une commande PowerShell a un utilisateur sous bash l'envoie
# dans le mur, et reciproquement.
#
# os.name ne suffit pas : il decrit l'INTERPRETEUR, pas l'outillage appelant.
# Un Python Windows lance depuis Git Bash rapporte « nt » alors que
# l'utilisateur vient de taper config-tool.sh. L'appelant, lui, sait toujours :
# il le declare via --hint-style. La detection ne sert que de defaut.
HINTS = {
    "sh":  {"reset": "sudo ./scripts/linux/config-tool.sh reset",
            "merge": "sudo ./scripts/linux/config-tool.sh merge"},
    "ps1": {"reset": r".\scripts\windows\config-tool.ps1 reset",
            "merge": r".\scripts\windows\config-tool.ps1 merge"},
}
DEFAULT_STYLE = "ps1" if os.name == "nt" else "sh"


def known_types(binary):
    """Types de modules que le binaire sait construire, ou None si indisponible."""
    if not binary:
        return None
    try:
        out = subprocess.run([binary, "--list-types"], capture_output=True,
                             text=True, timeout=10)
    except (OSError, subprocess.SubprocessError):
        return None
    if out.returncode != 0:
        return None
    # « Types de modules disponibles : monitor, autre »
    line = out.stdout.strip()
    if ":" not in line:
        return None
    return [t.strip() for t in line.split(":", 1)[1].split(",") if t.strip()]


def load(path, label):
    try:
        with open(path, encoding="utf-8-sig") as fh:
            return json.load(fh)
    except OSError as exc:
        print(f"{ERR} {label} illisible : {exc}")
        sys.exit(2)
    except ValueError as exc:
        print(f"{ERR} {label} n'est pas un JSON valide : {exc}")
        print(f"        Le service refusera de demarrer. Corriger la syntaxe.")
        sys.exit(2)


def main():
    ap = argparse.ArgumentParser(add_help=True)
    ap.add_argument("config")
    ap.add_argument("--binary", help="binaire du service, interroge via --list-types")
    ap.add_argument("--example", help="configuration d'exemple, pour comparaison")
    ap.add_argument("--hint-style", choices=sorted(HINTS), default=DEFAULT_STYLE,
                    help="outillage a citer dans les conseils (sh ou ps1)")
    args = ap.parse_args()
    hint = HINTS[args.hint_style]

    cfg = load(args.config, "La configuration")
    fatal = False

    # --- Les modules : c'est ici que le service se joue ---------------------
    modules = cfg.get("modules")
    if not isinstance(modules, list) or not modules:
        print(f"{ERR} Aucun module declare.")
        print("        Le service demarrera mais ne supervisera rien : toutes les")
        print("        routes /api/ repondront 503.")
        fatal = True
        modules = []

    valid = known_types(args.binary)
    if valid is None and args.binary:
        print(f"{WARN} Types valides indeterminables (binaire injoignable ou trop ancien).")
    elif valid is None:
        print(f"{INFO} Types valides non verifies : passer --binary pour les controler.")

    declared = [m.get("type") for m in modules if isinstance(m, dict)]
    if valid is not None:
        unknown = [t for t in declared if t not in valid]
        usable = [t for t in declared if t in valid]

        for t in unknown:
            print(f"{ERR} Type de module inconnu : '{t}'.")
            print(f"        La fabrique ne connait que : {', '.join(valid)}.")
            print("        Ce module ne sera PAS construit ; le service ne le dira")
            print("        qu'au demarrage (journalctl -u morfmonitor).")

        if not usable:
            print(f"{ERR} Aucun module exploitable : le service n'aura rien a exposer.")
            print("        Corriger le champ 'type' dans la section 'modules', ou")
            print("        reinitialiser la configuration :")
            print(f"            {hint['reset']}")
            fatal = True
        else:
            print(f"{OK} Modules exploitables : {', '.join(usable)}.")
    elif declared:
        print(f"{INFO} Modules declares : {', '.join(str(t) for t in declared)}.")

    # --- Comparaison a l'exemple : informatif, jamais bloquant --------------
    if args.example:
        ex = load(args.example, "L'exemple")
        missing = [k for k in ex
                   if not k.startswith("_comment") and k not in cfg]
        if missing:
            print(f"{WARN} Cles absentes de la configuration deployee : {', '.join(missing)}.")
            print("        Les ajouter sans toucher aux valeurs existantes :")
            print(f"            {hint['merge']}")

        ex_types = {m.get("type") for m in ex.get("modules", []) if isinstance(m, dict)}
        new_types = sorted(t for t in ex_types if t and t not in declared)
        if new_types:
            print(f"{INFO} L'exemple propose des modules absents ici : {', '.join(new_types)}.")
            print("        Ajout volontaire uniquement : un module non demande ne")
            print("        doit pas s'activer tout seul.")

    # --- Exposition : constat, pas prescription ----------------------------
    if cfg.get("web_enabled", True) and cfg.get("bind_address") == "0.0.0.0":
        print(f"{INFO} Interface Web exposee sur toutes les interfaces (0.0.0.0).")
        print("        Convient a un LAN de confiance. Sur une machine multi-domiciliee,")
        print("        preferer l'adresse du reseau local.")

    if fatal:
        print()
        print(f"{ERR} Configuration INEXPLOITABLE en l'etat.")
        return 1

    print(f"{OK} Configuration exploitable.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
