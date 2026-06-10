# VigiPoule v4 (Padoue)
Projet porte de poulailler automatique (04/2026)

## 1. PRESENTATION

Le VigiPoule est un dispositif d’ouverture et de fermeture automatique de porte de poulailler.
La v4 « Padoue » du VigiPoule garde le coeur du fonctionnement à base d’Arduino Nano mais intégre plusieurs changement majeurs :

1. l’ajout du module horaire DS3231 permet une ouverture/fermeture plus fiable, par tous les temps, de la porte avec l’utilisation d’horaires pré-enregistrés (eeprom) differents selon le mois de l’année (*ce qui est une fonctionalité non proposée dans les appareils du commerce à ma connaissance*).  
Le capteur de lumère reste présent en mode dégradé si défaillance du module horaire.
3. le remplacement du moteur NEMA17 par un JGY-370 plus adapté (couple/vitesse/ maintien de la position) avec pilotage par L9110H (800mA)
4. le remplacement des capteurs mécaniques fin de course qui vieillissent mal en extérieur par un capteur effet hall interne, déclenché par un aimant sur le haut de la porte.
5. la pose d’une prise RJ11 pour le branchement de la ServiceBox permettant le monitoring temps réel, la lecture/modification des paramétres eeprom, la vérification du log d’erreurs et le reset usine
6. le design du PCB sur Fritzing et sa gravure en usine pour une finition parfaite (avec un emplacement pour future module bluetooth)

On y retrouve un bouton unique, 2 led, 2 microswitch dip, 1 fiche numérique pour option future mais pas d’ecran.

L’effort a été fait sur une mise en service et un fonctionnement simplifié

## 2. UTILISATION

### Mise en service :
Mettre la porte manuellement en position basse mettre en DipSwitch 1 sur 0 (position calibration)  
Mettre sous tension (*l’EEPROM est vide, elle sera remplie avec les paramètres usine*)  
Utiliser le bouton jaune pour ajuster la porte en bas (appui court pour descendre, moyen pour remonter)  
Appuyer longuement (5 sec) pour valider la position basse => la porte remonte jusqu’en haut  
Mettre le DipSwitch 1 sur 1 (position fonctionnement)  
(*le dipswitch 2 n'est pas utilisé pour le moment*)

### Fonctionnement :
fonctionnement automatique, aucune intervention utilisateur dès la mise en service  
bouton poussoir pour *ouvrir/fermer* manuellement la porte si besoin

Mode dégradé :
si problème d’horloge, la cellule photoelectrique prend le relais (*warning 2 ou 3*) pour piloter l’ouverture/ fermeture de la porte
