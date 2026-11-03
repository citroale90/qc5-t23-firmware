# Firmware locale VeePai QC5 T23

Il progetto riguarda la **QC5_YA_CODE_V82** con Ingenic T23, GC1084,
AIC8800DC USB e flash NOR da 8 MiB. R4g5b-i18n1 è la versione bilingue
collaudata su una camera aggiornata da R4g5b.

La WebUI offre italiano e inglese, video locale con audio e interfono,
controllo PTZ, luce bianca e IR, registrazione SD e configurazione Wi-Fi
tramite hotspot. La sezione Home Assistant genera i comandi da importare
e mostra il flusso RTSP da inserire in Generic Camera.

Per generare la WebUI esegui `python3 build.py`. Per assemblare il
pacchetto da SD servono le immagini locali elencate nella
[guida alla build](build.md). I dump includono i dati privati della singola
camera e vanno conservati fuori dal repository.

[README in English](../README.md) · [Attribuzioni](../NOTICE.md)
