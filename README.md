# flibuste-align

Équivalent libre d'un outil de type VocAlign : à partir d'une prise **guide** et d'une prise **double** (surtout des voix chantées), la bibliothèque trace la correspondance des deux dans le temps et étire le double pour qu'il tombe sur le guide, **sans changer sa hauteur**. Mono, ou stéréo. Projet de la Flibuste Suite.

> Statut : en développement, aucune version publiée. Les mesures sont faites sur des voix synthétiques à vérité connue (début de note : erreur moyenne d'environ 3 ms) et sur quelques essais de vraies voix ; voir `docs/README.md`.

## Ce qu'il y a dedans
- **`core/`** : la bibliothèque `align_*` (analyse, chemin d'appariement, protection des consonnes, force réglable) et l'**interface C** `core/include/align/align_c.h` (version 2), dont `align_warp_render`, un étirement par marqueurs sans guide.
- **`tools/align_cli`** : l'outil en ligne de commande (`align_cli guide.wav double.wav sortie.wav`).
- **`adapter-juce/`** : le plugin CLAP **Sillage** (nom provisoire).
- **`docs/bundle/`** et `tools/make_daw_bundle.py` : le paquet de sources autonome (`dist/align-native/`, à générer) pour intégrer l'interface C dans un autre programme.
- Trois étireurs au choix : Signalsmith Stretch (par défaut), Rubber Band R2 et R3.

## Compiler
Il faut CMake et un compilateur C++17 ; JUCE et Catch2 sont téléchargés par CMake.

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build
```

`-DALIGN_BUILD_PLUGIN=OFF` pour ne compiler que la bibliothèque, les outils et les tests.

## Licences
- **GNU GPL v3 ou ultérieure** pour la bibliothèque, l'outil, l'interface C, les tests et les outils (`LICENSE`).
- **GNU AGPL v3 ou ultérieure** pour le plugin Sillage, parce qu'il utilise JUCE 8 (`adapter-juce/LICENSE.md`).
- Composants tiers : Rubber Band (GPL v2 ou ultérieure), Signalsmith Stretch et Linear (MIT), dr_wav (domaine public ou MIT-0), voir `third_party/README.md`.

Chaque fichier source porte son identifiant SPDX en première ligne.
