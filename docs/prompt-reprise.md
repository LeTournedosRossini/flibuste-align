# Prompt de reprise : projet flibuste-align (calage de doublures, type VocAlign)

Colle ce texte au début de la nouvelle discussion.

---

Tu reprends le projet **flibuste-align** dans `/home/jean/Bureau/FlibusteSuite/flibuste-align`. Réponds-moi en français, simplement, en disant ce qui est fait et ce qui est vérifié ou non. Commence par lire `docs/README.md`, la mémoire (`project-flibuste-align.md`), `echanges/etat-versions.md` et le dernier message pour DAW : `echanges/messages/2026-11-08_1915_de-neptune_pour-daw-interface-c-livree-align-native.md`.

## Le projet
Équivalent libre (GPLv3 ou ultérieure ; plugin Sillage en AGPLv3 ou ultérieure à cause de JUCE) de VocAlign : caler une prise « double » sur une prise « guide » sans changer la hauteur, surtout pour des voix chantées, en mono (stéréo si robuste). Trois livrables :
- la bibliothèque `align_*` et l'outil `align_cli` ;
- le plugin CLAP **Sillage** (nom provisoire) pour les autres hôtes comme Reaper ;
- l'**interface C `align_c.h`** (paquet `dist/align-native/`, version 1), que DAW (mon hôte en Rust + Tauri, dans `/home/jean/Bureau/DAW perso`) intégrera directement, avec une interface dans le style DAW et non Flibuste.

## Décisions déjà prises (ne pas les redemander)
- Moteur par défaut : Signalsmith Stretch ; Rubber Band R2 et R3 au choix.
- Force réglable, avec courbe de force variable par double et suggestion automatique de la courbe.
- Protection des consonnes, avec zones de consonnes modifiables à la main.
- Copier-coller des **réglages généraux seulement** : la courbe de force et les consonnes modifiées ne se copient jamais.
- Pas de serrage variable. Pas de copier-coller entre instances du plugin.
- Le guide est toujours la même voix. Paroles différentes : non prévu (on peut essayer). Pas de calage des doubles entre eux. 3 minutes maximum.
- Harmonies (mêmes mots, autre hauteur) : mode d'analyse « harmonies » ; le mode d'analyse appartient au guide, donc au plus deux guides par lot (un par mode).
- Plugin, deuxième voix : exporter le premier double, « Tout effacer », capturer la voix suivante contre le même guide.

## État au 2026-11-08
- Bibliothèque, CLI, plugin Sillage (look NOVA, suggestion de force, mode harmonies, nom de voix) et interface C : faits, testés (21 cas de test ; démonstration en C pur sous GCC et MSVC : erreur moyenne 2,7 ms sur les débuts de notes, pire cas 6,5 ms, rendu identique après resérialisation de l'analyse, annulation propre).
- Sillage installé sous Linux (`~/.clap/`). Dernier état sous Windows compilé et testé en test sans interface ; **à écouter dans Reaper** (fermer et relancer Reaper).
- Commits locaux seulement (derniers : 84e709b puis a479279). **Rien n'est poussé sur GitHub, aucune CI lancée.**
- **2026-11-11 : interface C version 2**, `align_warp_render` (étirement par marqueurs demandé par DAW : couples source/sortie, rapport [0,25 ; 4], 10 ms minimum, trois moteurs, déterministe, annulation) ; 28 cas de test, démonstration C pur OK GCC et MSVC. Limites annoncées à DAW : transitoires ±4 ms (Signalsmith) et ±20 ms (Rubber Band), rapport 1 non intact avec Signalsmith, crêtes possibles au-delà de l'entrée. Message : `echanges/messages/2026-11-11_0020_de-neptune_pour-daw-align-warp-render-livree.md` (Jean le relaie). Détecteur de débuts de son : laissé à DAW sauf demande.
- Fichiers d'écoute déposés sur le bureau Windows, dossier « Tests calage » (guide et double de 30 s, quatre rendus, paire synthétique de 7 s).
- Message pour DAW prêt (18h15 / 19h15 : remplace les deux précédents). Jean le transmet à DAW et rapporte ses remarques.

## Pas encore vérifié
- Appel depuis Rust (`cc` dans `build.rs`) : DAW doit compiler le paquet.
- Mode harmonies sur de vraies voix (Jean n'a pas encore écouté `double-cale-mode-harmonies.mp3`).
- Plugin dans Reaper : suggestion, analyse, nom de voix, rendu « Apply FX to item », placement du BWF exporté.
- Guitare (option chroma testée sans gain, désactivée par défaut).

## À faire ensuite
1. Attendre les remarques de DAW (compilation, limites, format des octets d'analyse, option DLL) avant d'étendre l'interface C.
2. Selon l'écoute de Jean : choisir si le mode harmonies devient le défaut.
3. Éventuel : cible DLL (`ALIGN_SHARED`), tests de bounce Reaper, vraies harmonies, essais guitare.

## Règles de travail
- Compilations et tests : toujours via `~/.local/bin/claude-build <linux|windows> ...` (file d'attente) ; Windows par `ssh neptune-win` (`C:\Users\lcj\align-build`).
- Après chaque bonne compilation : installer sous Linux et Windows (Reaper fermé, ancien fichier sauvegardé dans `neptune-anciens/<horodatage>`, SHA256 comparé).
- Pousser sur GitHub **seulement si Jean le demande** ; CI rarement et avec son accord ; commits finissant par `[skip ci]` et `Co-Authored-By: Claude Sonnet 5.5 <noreply@anthropic.com>` ; identité git `Jean Eudes L'Hoste <je.lhoste@gmail.com>`.
- Les messages pour les autres conversations vont dans `echanges/messages/` ; Jean les relaie. Demander à Jean avant d'agir sur une décision venue d'une autre conversation. Tenir à jour `echanges/etat-versions.md`.
- Prévenir Jean quand une attente est longue.
