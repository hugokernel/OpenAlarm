OpenAlarm
=========

Un système d'alarme libre.

Caractéristiques :

- Uniquement basé sur des briques libres (OpenSource / OpenHardware)
- Autonomie très grande des modules capteurs, appelés « Node » (plusieurs mois)
- Les Node sont basés sur un µcontrolleur Atmel AVR ATMega32U4 et l'interface radio, sur un RFM12B (basé sur un Si4421 http://www.silabs.com/products/wireless/EZRadio/Pages/Si442021.aspx)
- Programmation et recharge simple des Node par USB (port µUSB inclu)
- Pas besoin de base : 2 Node suffisent à faire fonctionner une surveillance
- Possibilité de modifier les paramètres d'un Node à distance
- Technos utilisées : Arduino (Ino) pour les Node, et Python pour l'interface avec un PC
- Les entrées / sorties sont facilement accessible (au pas de 2.54mm)
- 2 leds (vertes et rouges) inclues sur chaque Node

Les premiers Node sont fonctionnel en version 1.5.0, les schémas de principe et PCB sont disponible en PDF dans le dossier https://github.com/hugokernel/OpenAlarm/blob/master/hardware/export/pdf/node/1.5.0/

Pour rester informer, ça se passe sur le site officiel d'OpenAlarm http://openalarm.net/, vous pouvez venir en parler sur le canal irc #madeinfr sur le réseau freenode.
