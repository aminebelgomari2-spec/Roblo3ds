# Roblo3DS

Un mini "Roblox" pour **Nintendo 3DS (normale)** fait en C avec libctru + citro2d.
3e personne, carte en blocs, personnage blocky, caméra tactile, 3 objets
fonctionnels qui cassent les blocs.

## Commandes

| Entrée | Action |
|---|---|
| Circle pad | Bouger le personnage |
| Glisser sur l'écran bas | Tourner la caméra (comme sur mobile) |
| B | Sauter |
| Croix gauche/droite | Changer de slot d'inventaire (1 Épée, 2 Bombe, 3 Lance-roquette) |
| L ou R | Utiliser l'objet (coups d'épée, bombe, roquette — tout casse des blocs) |
| START | Reconstruire la carte + respawn |

## Map

Baseplate en herbe 28x28, une colline, une tour en briques avec escalier,
une maison en bois. Les explosions creusent la carte (rayon 2.4~3 blocs).

## Construire

- Sur PC avec devkitARM : `make` (produit `Roblo3DS.3dsx` + `Roblo3DS.elf`),
  puis `make cia` (produit `Roblo3DS.cia` via `tools/make_cia.sh`).
- Ou simplement push sur GitHub : l'action `.github/workflows/build.yml`
  compile tout et publie `.3dsx`, `.cia` et le QR code dans les Releases
  (tag fixe `latest`).

## Assets

Les 3 fichiers d'assets requis par le CIA sont déjà dans `assets/` :

- `icon.png` : 48x48 (icône menu HOME)
- `banner.png` : 256x128 (bannière)
- `audio.wav` : PCM 16 bits mono ~1.2 s (son de la bannière)

Pour les regénérer : `python3 tools/gen_assets.py` (stdlib pur, aucune
dépendance). Remplace-les par des vrais dessins si tu veux, en gardant
exactement les mêmes dimensions/formats.
