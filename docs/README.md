# flibuste-align — caler une doublure sur un guide

Projet de la Flibuste Suite (GPLv3). Équivalent libre d'un outil de type VocAlign : on donne un **guide** et un **double**, on rend le double recalé sur le guide, sans changer sa hauteur.

## État (2026-11-08)
- `core/` : `align::analyse` (coefficients cepstraux sur échelle mel, un trait tous les 5 ms), `align::alignPath` (recherche du chemin d'appariement dans une bande de ±1,5 s, vitesse du double entre ×0,5 et ×2, réglage de « serrage »), `align::makeWarpMap` (courbe temps de sortie -> échantillon source). Aucune dépendance.
- `tests/` : Catch2, sur des voix synthétiques dont la vraie correspondance est connue (`SynthVoice.h`). Début de note : erreur moyenne ~3 ms, maximum < 20 ms.
- `tools/stretch_eval` : compare Rubber Band (R3 et R2) et Signalsmith Stretch (rendu avec une cadence variable) et écrit les WAV pour l'écoute.

## Étireurs
- **Rubber Band** (GPL 2+, `third_party/rubberband`, un seul fichier à compiler) : carte de points clés entrée -> sortie, hors temps réel. Des points tous les ~45 ms.
- **Signalsmith Stretch** (MIT, `third_party/signalsmith-stretch`, en-têtes seuls) : on lui donne, bloc par bloc, la quantité d'entrée voulue. Retard constant à compenser (56,2 ms à 44,1 kHz).

## Rendu (`align::render`)
Trois moteurs derrière une seule fonction : Signalsmith (par défaut), Rubber Band R2, Rubber Band R3. Garanties, vérifiées par les tests : longueur de sortie exacte, valeurs finies, nombre de voies conservé (la même courbe sert toutes les voies : l'image stéréo reste), cas limites (silence, extrait très court, entrée vide). Si un moteur Rubber Band échoue, le rendu retombe sur Signalsmith (`fellBack`).
Mesures sur 8 doublures synthétiques : calage Signalsmith 3,9 ms, R2 5,2 ms, R3 12 ms. Rubber Band R3 donnait un rapport NaN avec le point clé (0, 0) : retiré, R3 est passé de 21 à 12 ms.

## Consonnes
`align::findConsonants` repère les zones bruitées riches en aigus (liste de `Segment` début/fin en secondes, faite pour être modifiée par une interface). `makeWarpMap (..., consonants, protect)` joue ces zones à leur vitesse d'origine (protect = 1) en gardant l'attaque de la voyelle suivante calée sur le guide ; le temps est reporté sur ce qui précède. Sur voix synthétique : 43 consonnes trouvées sur 43, aucune fausse zone, durée conservée dans 43 cas sur 43. Ces voix ne montrent presque aucun gain à l'oreille (leurs consonnes durent 50 ms dans les deux prises) : le vrai réglage se fera sur des prises réelles. Rubber Band R3 perd en calage quand la protection est active (points clés trop espacés pour des zones de 20-50 ms) : à revoir.

## Morceau entier (essai sur 127 s synthétiques, stéréo)
Alignement 7,4 s, mémoire 290 Mo ; rendu Signalsmith 2,9 s, R2 4,7 s, R3 17,7 s. Erreur moyenne ~14 ms sur 120 notes enchaînées avec des décalages indépendants de ±100 ms à chaque note (cas plus dur que des doublures réelles) : les écarts de pause très différents sont limités par la pente maximale (×0,5 à ×2). Les traits de hauteur (`chromaWeight`) n'ont pas amélioré ces mesures : désactivés par défaut.
Double qui commence 0,8 s en retard (bouts libres) : erreur moyenne 19 ms sur le même morceau.

## Outil en ligne de commande
`align_cli guide.wav double.wav sortie.wav [--engine signalsmith|r2|r3] [--protect 0..1] [--tightness 0..1] [--max-shift s] [--chroma poids] [--open-ends] [--write-consonants f.txt]` : lit du WAV mono ou stéréo (même fréquence pour les deux), écrit un WAV 32 bits flottant de la durée du guide, et affiche décalage, vitesse et temps de calcul.

## À faire
- Écouter sur de vraies prises (voix, guitare), choisir l'étireur (ou les deux, au choix de l'utilisateur).
- Étirer moins les consonnes (ancrer les attaques), réglage « souffle / silences ».
- Stéréo : analyse sur la somme, même courbe appliquée aux deux voies.
- Dans DAW : opération sur deux extraits ; plugin CLAP : mode capture avec entrée latérale.
