# Firmware locale VeePai QC5 T23

Il progetto riguarda la **QC5_YA_CODE_V82** con Ingenic T23, GC1084,
AIC8800DC USB e flash NOR da 8 MiB. La versione nativa installata e
collaudata è **QC5 Linux 7.3.1 R43 RC1**, del 13 novembre 2026.

R43 conserva nella flash le credenziali Wi-Fi e la chiave del server SSH,
senza dipendere dalla SD. La chiave già conosciuta dal client rimane la
stessa, con due copie verificate. Un riavvio normale ha riconfermato
Wi-Fi senza QR, identità SSH, WebUI e video/audio 1280×720. Il caricamento
della chiave senza driver SD è stato provato in un avvio temporaneo in RAM.
La SD serve ancora per le registrazioni locali e le funzioni basate su file.

I sorgenti nativi sono in [`native/`](../native/README.md), con avvio,
persistenza, installatore/ripristino, correzione dell'interfono e WebUI
effettivamente impiegata. Il builder principale conserva la precedente
linea **R4g5b-i18n1 / Linux 3.10**: i suoi pacchetti non installano R43.
L'esportazione nativa non è ancora una build completa riproducibile da un
clone del repository; gli ingressi mancanti sono documentati.

La WebUI offre italiano e inglese, video locale con audio e interfono,
controllo PTZ, luce bianca e IR, registrazione SD e configurazione Wi-Fi
tramite hotspot. La sezione Home Assistant genera il pacchetto dei comandi
e lo YAML della scheda. Dopo aver importato il pacchetto, incolla lo YAML in
**Aggiungi scheda → Manuale** per controllare la camera dalla dashboard.
Il flusso RTSP si configura in Generic Camera.

Per generare la WebUI esegui `python3 build.py`. Per assemblare il
pacchetto da SD servono le immagini locali elencate nella
[guida alla build](build.md). I dump includono i dati privati della singola
camera e vanno conservati fuori dal repository.

[README in English](../README.md) · [Attribuzioni](../NOTICE.md)

La WebUI è open source. Il firmware completo non è ancora interamente
open source: rimangono firmware binari del Wi-Fi, U-Boot originale e
verifiche di provenienza/licenza su alcuni ingressi dei driver. I backup,
le credenziali, le chiavi e i pacchetti specifici di una camera non sono
pubblicati. Dettagli in [stato open source](open-source-status.md).
