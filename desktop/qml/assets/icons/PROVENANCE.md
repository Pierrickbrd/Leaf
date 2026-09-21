# Les quatre icônes de la barre

`search.svg`, `tune.svg`, `sort.svg`, `settings.svg` sont des **Material Symbols Rounded**,
tels que Google les publie, non modifiés — la graisse 400, la taille optique 24, sans
remplissage ni gradation. Ce sont les glyphes que *L'architecture du client* nomme un par un
pour la barre, et les mêmes que Pronos SRFC utilise, ce qui est la raison de les avoir choisis.

    https://github.com/google/material-design-icons — symbols/web/<nom>/materialsymbolsrounded/

**Licence Apache 2.0**, celle du dépôt d'origine. Elle demande que l'origine soit nommée, ce
que ce fichier fait, et permet l'usage tel quel.

Ils n'ont **pas** d'attribut `fill` : le chemin sort en noir et c'est le client qui le teinte,
par `ColorOverlay`, avec le ton du thème. C'est pour cela qu'un seul fichier par icône suffit
là où les ombres des couvertures en demandent deux — une teinte se calcule, une ombre non.
